// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PluginInventoryEntry.h"

class IHttpRequest;
class IHttpResponse;

/**
 * FOllamaPluginSummaryProvider
 *
 * Async HTTP client that talks to a local Ollama instance
 * (http://localhost:11434) to generate AI-powered summaries for plugins.
 *
 * - Fire-and-forget: call RequestSummary(); result arrives via delegate.
 * - Results cached per (plugin fingerprint + "|" + ModelName) in
 *   <ProjectSaved>/PluginInventoryBrowser/SummaryCache.json.
 * - If Ollama is offline or the model is missing the fallback path produces
 *   a deterministic metadata-based summary without blocking.
 */
class FOllamaPluginSummaryProvider : public TSharedFromThis<FOllamaPluginSummaryProvider>
{
public:
	DECLARE_DELEGATE_ThreeParams(FOnSummaryReady,
		const FString& /* PluginName */,
		const FString& /* SummaryText */,
		bool           /* bWasAI */);

	DECLARE_DELEGATE_OneParam(FOnModelsReady,
		const TArray<FString>& /* ModelNames */);

	FOllamaPluginSummaryProvider();
	~FOllamaPluginSummaryProvider();

	/**
	 * Request a summary for the given entry using the specified model.
	 * OnReady fires on the game thread when complete (success or fallback).
	 * If a cached result exists the delegate fires synchronously before returning.
	 */
	void RequestSummary(
		const FPluginInventoryEntryRef& Entry,
		const FString& ModelName,
		FOnSummaryReady OnReady);

	/**
	 * Query Ollama for available models (GET /api/tags).
	 * OnReady fires on the game thread with the list (may be empty if offline).
	 */
	void FetchAvailableModels(FOnModelsReady OnReady);

	/**
	 * Evict any cached summaries that contain the given model name.
	 * Call this when the user changes the selected model.
	 */
	void InvalidateCacheForModel(const FString& ModelName);

	/** Returns a deterministic fallback summary built from entry metadata only. */
	static FString BuildFallbackSummary(const FPluginInventoryEntryRef& Entry);

private:
	static constexpr const TCHAR* OllamaBaseUrl = TEXT("http://127.0.0.1:11434");

	/** In-memory summary cache: key = PluginName + "_" + ModelName */
	TMap<FString, FString> SummaryCache;

	/** Whether the cache has unsaved changes. */
	bool bCacheDirty = false;

	FString GetCacheFilePath() const;
	void LoadCacheFromDisk();
	void SaveCacheToDisk();

	static FString BuildSummaryPrompt(const FPluginInventoryEntryRef& Entry);

	void OnGenerateResponse(
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully,
		FString CacheKey,
		FString PluginName,
		FOnSummaryReady OnReady,
		FPluginInventoryEntryRef Entry);

	void OnTagsResponse(
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bConnectedSuccessfully,
		FOnModelsReady OnReady);
};
