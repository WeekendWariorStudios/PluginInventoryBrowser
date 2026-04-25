// Copyright StateOfRuin, 2026. All Rights Reserved.

#include "OllamaPluginSummaryProvider.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"

namespace
{
	FString BuildEntryFingerprintSeed(const FPluginInventoryEntryRef& Entry)
	{
		const FString TruncDescription = Entry->Description.Len() > 500
			? Entry->Description.Left(500)
			: Entry->Description;

		return FString::Printf(
			TEXT("%s|%s|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%s"),
			*Entry->Name,
			*Entry->FriendlyName,
			*TruncDescription,
			*Entry->Category,
			*Entry->CreatedBy,
			*Entry->EngineVersion,
			*Entry->VersionName,
			*Entry->BaseDir,
			*Entry->DescriptorFileName,
			static_cast<int32>(Entry->PluginType),
			static_cast<int32>(Entry->LoadedFrom),
			Entry->bIsEnabled ? 1 : 0,
			Entry->ModuleCount,
			Entry->DependencyCount,
			*FString::Join(Entry->SupportedTargetPlatforms, TEXT(",")));
	}

	FString BuildEntryFingerprint(const FPluginInventoryEntryRef& Entry)
	{
		return FString::Printf(TEXT("%08X"), FCrc::StrCrc32(*BuildEntryFingerprintSeed(Entry)));
	}
}

// ============================================================================
// Construction / Destruction
// ============================================================================

FOllamaPluginSummaryProvider::FOllamaPluginSummaryProvider()
{
	LoadCacheFromDisk();
}

FOllamaPluginSummaryProvider::~FOllamaPluginSummaryProvider()
{
	if (bCacheDirty)
	{
		SaveCacheToDisk();
	}
}

// ============================================================================
// Public API
// ============================================================================

void FOllamaPluginSummaryProvider::RequestSummary(
	const FPluginInventoryEntryRef& Entry,
	const FString& ModelName,
	FOnSummaryReady OnReady)
{
	const FString CacheKey = BuildEntryFingerprint(Entry) + TEXT("|") + ModelName;

	// Serve from cache if present
	if (const FString* Cached = SummaryCache.Find(CacheKey))
	{
		OnReady.ExecuteIfBound(Entry->Name, *Cached, true);
		return;
	}

	// Build the JSON payload for /api/generate (non-streaming)
	FString SystemPrompt;
	FString UserPrompt;
	BuildSummaryPrompt(Entry, SystemPrompt, UserPrompt);

	// Generation options – allow a generous token budget for 1-2 paragraphs
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetNumberField(TEXT("num_predict"), 400);
	Options->SetNumberField(TEXT("temperature"), 0.4);
	Options->SetNumberField(TEXT("top_p"),       0.9);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"),   ModelName);
	Body->SetStringField(TEXT("system"),  SystemPrompt);
	Body->SetStringField(TEXT("prompt"),  UserPrompt);
	Body->SetBoolField  (TEXT("stream"),  false);
	Body->SetObjectField(TEXT("options"), Options);

	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(FString(OllamaBaseUrl) + TEXT("/api/generate"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(BodyString);

	// Capture entry by value so it outlives the lambda
	FPluginInventoryEntryRef EntryCopy = Entry;
	Request->SetTimeout(120.f);
	Request->OnProcessRequestComplete().BindSP(
		AsShared(),
		&FOllamaPluginSummaryProvider::OnGenerateResponse,
		CacheKey,
		Entry->Name,
		OnReady,
		EntryCopy);

	if (!Request->ProcessRequest())
	{
		// Could not even dispatch: fall back immediately
		const FString Fallback = BuildFallbackSummary(Entry);
		OnReady.ExecuteIfBound(Entry->Name, Fallback, false);
	}
}

void FOllamaPluginSummaryProvider::FetchAvailableModels(FOnModelsReady OnReady)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(FString(OllamaBaseUrl) + TEXT("/api/tags"));
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(10.f);
	Request->OnProcessRequestComplete().BindSP(
		AsShared(),
		&FOllamaPluginSummaryProvider::OnTagsResponse,
		OnReady);

	if (!Request->ProcessRequest())
	{
		OnReady.ExecuteIfBound(TArray<FString>{});
	}
}

void FOllamaPluginSummaryProvider::InvalidateCacheForModel(const FString& ModelName)
{
	TArray<FString> KeysToRemove;
	for (const auto& Pair : SummaryCache)
	{
		if (Pair.Key.EndsWith(TEXT("|") + ModelName))
		{
			KeysToRemove.Add(Pair.Key);
		}
	}
	if (KeysToRemove.Num() > 0)
	{
		for (const FString& Key : KeysToRemove)
		{
			SummaryCache.Remove(Key);
		}
		bCacheDirty = true;
		SaveCacheToDisk();
	}
}

// ============================================================================
// Fallback / Prompt builders
// ============================================================================

/*static*/ FString FOllamaPluginSummaryProvider::BuildFallbackSummary(const FPluginInventoryEntryRef& Entry)
{
	FString Summary;
	Summary += FString::Printf(TEXT("%s"), *Entry->FriendlyName);

	if (!Entry->CreatedBy.IsEmpty())
	{
		Summary += FString::Printf(TEXT(" by %s"), *Entry->CreatedBy);
	}

	if (!Entry->Category.IsEmpty() && Entry->Category != TEXT("Other"))
	{
		Summary += FString::Printf(TEXT(" (Category: %s)"), *Entry->Category);
	}

	Summary += TEXT(".\n\n");

	if (!Entry->Description.IsEmpty())
	{
		const FString& Desc = Entry->Description;
		Summary += Desc.Len() > 500 ? Desc.Left(497) + TEXT("…") : Desc;
		Summary += TEXT("\n\n");
	}

	TArray<FString> Details;
	Details.Add(FString::Printf(TEXT("%d module(s)"), Entry->ModuleCount));
	Details.Add(FString::Printf(TEXT("%d dependenc%s"), Entry->DependencyCount, Entry->DependencyCount == 1 ? TEXT("y") : TEXT("ies")));

	if (Entry->SupportedTargetPlatforms.Num() > 0)
	{
		Details.Add(TEXT("Platforms: ") + FString::Join(Entry->SupportedTargetPlatforms, TEXT(", ")));
	}
	if (!Entry->VersionName.IsEmpty())
	{
		Details.Add(TEXT("Version: ") + Entry->VersionName);
	}

	Summary += FString::Join(Details, TEXT(" · "));
	Summary += TEXT("\n\n(AI summary unavailable – Ollama not reachable or model not installed.)");
	return Summary;
}

/*static*/ FString FOllamaPluginSummaryProvider::BuildSummaryPrompt(const FPluginInventoryEntryRef& Entry)
{
	const FString& Desc = Entry->Description;
	const FString TruncDesc = Desc.Len() > 500 ? Desc.Left(497) + TEXT("…") : Desc;

	const FString PlatformList = Entry->SupportedTargetPlatforms.Num() > 0
		? FString::Join(Entry->SupportedTargetPlatforms, TEXT(", "))
		: TEXT("unspecified");

	return FString::Printf(
		TEXT("You are a helpful assistant for Unreal Engine developers.\n")
		TEXT("Summarize the following Unreal Engine plugin in 2-3 concise sentences.\n")
		TEXT("Focus on what the plugin does and who would use it.\n")
		TEXT("Do not repeat the plugin name or author as the first word.\n\n")
		TEXT("Plugin name: %s\n")
		TEXT("Friendly name: %s\n")
		TEXT("Category: %s\n")
		TEXT("Author: %s\n")
		TEXT("Version: %s\n")
		TEXT("Modules: %d\n")
		TEXT("Dependencies: %d\n")
		TEXT("Supported platforms: %s\n")
		TEXT("Description: %s\n\n")
		TEXT("Summary:"),
		*Entry->Name,
		*Entry->FriendlyName,
		*Entry->Category,
		*Entry->CreatedBy,
		*Entry->VersionName,
		Entry->ModuleCount,
		Entry->DependencyCount,
		*PlatformList,
		*TruncDesc);
}

// ============================================================================
// HTTP response handlers
// ============================================================================

void FOllamaPluginSummaryProvider::OnGenerateResponse(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> /*Request*/,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	bool bConnectedSuccessfully,
	FString CacheKey,
	FString PluginName,
	FOnSummaryReady OnReady,
	FPluginInventoryEntryRef Entry)
{
	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		OnReady.ExecuteIfBound(PluginName, BuildFallbackSummary(Entry), false);
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		OnReady.ExecuteIfBound(PluginName, BuildFallbackSummary(Entry), false);
		return;
	}

	FString SummaryText;
	if (!JsonObj->TryGetStringField(TEXT("response"), SummaryText) || SummaryText.IsEmpty())
	{
		OnReady.ExecuteIfBound(PluginName, BuildFallbackSummary(Entry), false);
		return;
	}

	// Cache and persist
	SummaryCache.Add(CacheKey, SummaryText);
	bCacheDirty = true;
	SaveCacheToDisk();

	OnReady.ExecuteIfBound(PluginName, SummaryText, true);
}

void FOllamaPluginSummaryProvider::OnTagsResponse(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> /*Request*/,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	bool bConnectedSuccessfully,
	FOnModelsReady OnReady)
{
	TArray<FString> ModelNames;

	if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		OnReady.ExecuteIfBound(ModelNames);
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		OnReady.ExecuteIfBound(ModelNames);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ModelsArray = nullptr;
	if (!JsonObj->TryGetArrayField(TEXT("models"), ModelsArray) || ModelsArray == nullptr)
	{
		OnReady.ExecuteIfBound(ModelNames);
		return;
	}

	for (const TSharedPtr<FJsonValue>& ModelValue : *ModelsArray)
	{
		const TSharedPtr<FJsonObject>* ModelObj = nullptr;
		if (ModelValue.IsValid() && ModelValue->TryGetObject(ModelObj) && ModelObj != nullptr)
		{
			FString ModelName;
			if ((*ModelObj)->TryGetStringField(TEXT("name"), ModelName) && !ModelName.IsEmpty())
			{
				ModelNames.Add(ModelName);
			}
		}
	}

	OnReady.ExecuteIfBound(ModelNames);
}

// ============================================================================
// Cache persistence
// ============================================================================

FString FOllamaPluginSummaryProvider::GetCacheFilePath() const
{
	return FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("PluginInventoryBrowser"),
		TEXT("SummaryCache.json"));
}

void FOllamaPluginSummaryProvider::LoadCacheFromDisk()
{
	const FString CachePath = GetCacheFilePath();
	if (!FPaths::FileExists(CachePath))
	{
		return;
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *CachePath))
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return;
	}

	for (const auto& Pair : JsonObj->Values)
	{
		FString Value;
		if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
		{
			SummaryCache.Add(Pair.Key, Value);
		}
	}
}

void FOllamaPluginSummaryProvider::SaveCacheToDisk()
{
	const FString CachePath = GetCacheFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);

	TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	for (const auto& Pair : SummaryCache)
	{
		JsonObj->SetStringField(Pair.Key, Pair.Value);
	}

	FString FileContent;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&FileContent);
	FJsonSerializer::Serialize(JsonObj, Writer);

	FFileHelper::SaveStringToFile(FileContent, *CachePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	bCacheDirty = false;
}
