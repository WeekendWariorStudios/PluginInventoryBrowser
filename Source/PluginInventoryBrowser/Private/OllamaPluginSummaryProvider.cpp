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

	// If a URL is available, fetch the page first for richer context
	const FString PluginURL = GetBestPluginURL(Entry);
	if (!PluginURL.IsEmpty())
	{
		FetchPageContent(PluginURL, CacheKey, ModelName, Entry->Name, OnReady, Entry);
	}
	else
	{
		RequestSummaryCore(Entry, ModelName, FString(), CacheKey, OnReady);
	}
}

void FOllamaPluginSummaryProvider::FetchPageContent(
	const FString& URL,
	const FString& CacheKey,
	const FString& ModelName,
	const FString& PluginName,
	FOnSummaryReady OnReady,
	FPluginInventoryEntryRef Entry)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("text/html,text/plain"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("PluginInventoryBrowser/1.0 (Unreal Engine Editor)"));
	Request->SetTimeout(10.f);
	Request->OnProcessRequestComplete().BindSP(
		AsShared(),
		&FOllamaPluginSummaryProvider::OnPageFetchResponse,
		CacheKey,
		ModelName,
		PluginName,
		OnReady,
		Entry);

	if (!Request->ProcessRequest())
	{
		RequestSummaryCore(Entry, ModelName, FString(), CacheKey, OnReady);
	}
}

void FOllamaPluginSummaryProvider::OnPageFetchResponse(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> /*Request*/,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	bool bConnectedSuccessfully,
	FString CacheKey,
	FString ModelName,
	FString /*PluginName*/,
	FOnSummaryReady OnReady,
	FPluginInventoryEntryRef Entry)
{
	FString PageContent;
	if (bConnectedSuccessfully && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		PageContent = StripHTML(Response->GetContentAsString());
	}
	RequestSummaryCore(Entry, ModelName, PageContent, CacheKey, OnReady);
}

void FOllamaPluginSummaryProvider::RequestSummaryCore(
	const FPluginInventoryEntryRef& Entry,
	const FString& ModelName,
	const FString& PageContent,
	const FString& CacheKey,
	FOnSummaryReady OnReady)
{
	FString SystemPrompt;
	FString UserPrompt;
	BuildSummaryPrompt(Entry, PageContent, SystemPrompt, UserPrompt);

	// Generation options – generous token budget for 2-5 paragraphs
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetNumberField(TEXT("num_predict"), 700);
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

	FPluginInventoryEntryRef EntryCopy = Entry;
	Request->SetTimeout(180.f);
	Request->OnProcessRequestComplete().BindSP(
		AsShared(),
		&FOllamaPluginSummaryProvider::OnGenerateResponse,
		CacheKey,
		Entry->Name,
		OnReady,
		EntryCopy);

	if (!Request->ProcessRequest())
	{
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

/*static*/ FString FOllamaPluginSummaryProvider::GetBestPluginURL(const FPluginInventoryEntryRef& Entry)
{
	if (!Entry->MarketplaceURL.IsEmpty()) return Entry->MarketplaceURL;
	if (!Entry->DocsURL.IsEmpty())        return Entry->DocsURL;
	if (!Entry->SupportURL.IsEmpty())     return Entry->SupportURL;
	if (!Entry->CreatedByURL.IsEmpty())   return Entry->CreatedByURL;
	return FString();
}

/*static*/ FString FOllamaPluginSummaryProvider::StripHTML(const FString& HTML)
{
	FString Result;
	Result.Reserve(HTML.Len());

	bool   bInTag  = false;
	FString TagBuf;

	const TCHAR* Data = *HTML;
	const int32  Len  = HTML.Len();

	for (int32 i = 0; i < Len; ++i)
	{
		const TCHAR Ch = Data[i];
		if (bInTag)
		{
			TagBuf += Ch;
			if (Ch == TEXT('>'))
			{
				bInTag = false;
				// Inject a space at block-level / br boundaries for readability
				const FString Lower = TagBuf.ToLower();
				if (Lower.Contains(TEXT("<br"))   || Lower.Contains(TEXT("/p>")) ||
				    Lower.Contains(TEXT("/div>")) || Lower.Contains(TEXT("/li>")) ||
				    Lower.Contains(TEXT("/h1>")) || Lower.Contains(TEXT("/h2>")) ||
				    Lower.Contains(TEXT("/h3>")) || Lower.Contains(TEXT("/tr>")))
				{
					Result += TEXT(" ");
				}
				TagBuf.Reset();
			}
		}
		else if (Ch == TEXT('<'))
		{
			bInTag = true;
			TagBuf.Reset();
			TagBuf += Ch;
		}
		else
		{
			Result += Ch;
		}
	}

	// Decode common HTML entities
	Result.ReplaceInline(TEXT("&amp;"),  TEXT("&"),  ESearchCase::CaseSensitive);
	Result.ReplaceInline(TEXT("&lt;"),   TEXT("<"),  ESearchCase::CaseSensitive);
	Result.ReplaceInline(TEXT("&gt;"),   TEXT(">"),  ESearchCase::CaseSensitive);
	Result.ReplaceInline(TEXT("&quot;"), TEXT("\""), ESearchCase::CaseSensitive);
	Result.ReplaceInline(TEXT("&apos;"), TEXT("'"),  ESearchCase::CaseSensitive);
	Result.ReplaceInline(TEXT("&nbsp;"), TEXT(" "),  ESearchCase::CaseSensitive);
	Result.ReplaceInline(TEXT("&#39;"),  TEXT("'"),  ESearchCase::CaseSensitive);

	// Collapse runs of whitespace, keep at most one blank line
	TArray<FString> Lines;
	Result.ParseIntoArray(Lines, TEXT("\n"), false);
	FString Cleaned;
	int32 BlankRun = 0;
	for (FString& Line : Lines)
	{
		while (Line.Contains(TEXT("  "))) { Line.ReplaceInline(TEXT("  "), TEXT(" ")); }
		Line = Line.TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			if (++BlankRun <= 1) { Cleaned += TEXT("\n"); }
		}
		else
		{
			BlankRun = 0;
			Cleaned += Line + TEXT("\n");
		}
	}

	Cleaned = Cleaned.TrimStartAndEnd();
	if (Cleaned.Len() > 3000)
	{
		Cleaned = Cleaned.Left(2997) + TEXT("…");
	}
	return Cleaned;
}

void FOllamaPluginSummaryProvider::BuildSummaryPrompt(
	const FPluginInventoryEntryRef& Entry,
	const FString& PageContent,
	FString& OutSystem,
	FString& OutUser)
{
	const FString& Desc = Entry->Description;
	const FString TruncDesc = Desc.Len() > 800 ? Desc.Left(797) + TEXT("…") : Desc;

	const FString PlatformList = Entry->SupportedTargetPlatforms.Num() > 0
		? FString::Join(Entry->SupportedTargetPlatforms, TEXT(", "))
		: TEXT("not specified");

	// ---- System role --------------------------------------------------------
	OutSystem =
		TEXT("You are an expert Unreal Engine developer writing plugin documentation for a plugin browser tool.\n")
		TEXT("When given plugin metadata, write a structured markdown summary using EXACTLY this format:\n")
		TEXT("\n")
		TEXT("## Core Purpose\n")
		TEXT("Explain what the plugin does, its key features, and the core problem it addresses. 2-4 sentences.\n")
		TEXT("\n")
		TEXT("## Use Cases\n")
		TEXT("List 3-5 concrete, practical scenarios where a developer would use this plugin.\n")
		TEXT("Start each item on a new line with \"- \".\n")
		TEXT("\n")
		TEXT("## Target Audience & Integration\n")
		TEXT("Describe who benefits most (e.g. gameplay programmer, technical artist, solo dev)\n")
		TEXT("and how the plugin integrates with Unreal Engine or complements other tools. 2-3 sentences.\n")
		TEXT("\n")
		TEXT("## Technical Notes\n")
		TEXT("(Include ONLY if relevant) Cover platform constraints, required setup, experimental status,\n")
		TEXT("known limitations, required engine version, or licensing. Omit this section entirely if\n")
		TEXT("no such information is available.\n")
		TEXT("\n")
		TEXT("Rules:\n")
		TEXT("  - Use ## for section headers EXACTLY as shown. No other header levels.\n")
		TEXT("  - Start use case list items with \"- \".\n")
		TEXT("  - Do NOT use bold (**), italic (*), or any other inline markdown decorators.\n")
		TEXT("  - Do NOT use tables, code blocks, or nested lists.\n")
		TEXT("  - Do NOT start \"Core Purpose\" with the plugin name or \"This plugin\".\n")
		TEXT("  - Third person, present tense throughout.\n")
		TEXT("  - Output ONLY the markdown sections above, nothing else.");

	// ---- User message -------------------------------------------------------
	FString PageSection;
	if (!PageContent.IsEmpty())
	{
		PageSection = FString::Printf(
			TEXT("\nAdditional context from the plugin's web page:\n---\n%s\n---\n"),
			*PageContent);
	}

	OutUser = FString::Printf(
		TEXT("Write a markdown-formatted summary for this Unreal Engine plugin.\n\n")
		TEXT("Name:        %s\n")
		TEXT("Friendly:    %s\n")
		TEXT("Category:    %s\n")
		TEXT("Author:      %s\n")
		TEXT("Version:     %s\n")
		TEXT("Modules:     %d\n")
		TEXT("Dependencies:%d\n")
		TEXT("Platforms:   %s\n")
		TEXT("Description: %s%s"),
		*Entry->Name,
		*Entry->FriendlyName,
		*Entry->Category,
		*Entry->CreatedBy,
		*Entry->VersionName,
		Entry->ModuleCount,
		Entry->DependencyCount,
		*PlatformList,
		*TruncDesc,
		*PageSection);
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
