#include "audio/StemSeparation.h"
#include "audio/StemSpectral.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

float testSignal (int i, int channel)
{
    const double t = (double) i / 44100.0;
    return (float) (0.4 * std::sin (juce::MathConstants<double>::twoPi * (110.0 + 37.0 * channel) * t)
                  + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 1234.5 * t)
                  + 0.05 * std::cos (0.37 * i));
}

juce::AudioBuffer<float> makeMix (int length)
{
    juce::AudioBuffer<float> mix (2, length);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < length; ++i)
            mix.setSample (ch, i, testSignal (i, ch));
    return mix;
}

float maxAbsDiff (const float* a, const float* b, int n)
{
    float worst = 0.0f;
    for (int i = 0; i < n; ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    return worst;
}

void stftRoundTripIsIdentity (bool hann, bool normalized, int signalSamples)
{
    StemStft stft (512, 128, hann, normalized, signalSamples);
    require (stft.getNumFrames() == 1 + signalSamples / 128, "frame count");
    const int n = signalSamples - 37;   // shorter input: zero-padded to the signal length
    auto mix = makeMix (n);

    std::vector<float> re (stft.getPlaneSize()), im (stft.getPlaneSize());
    std::vector<float> back ((size_t) n);
    stft.forward (mix.getReadPointer (0), n, re.data(), im.data());
    stft.inverse (re.data(), im.data(), back.data(), n);

    const auto err = maxAbsDiff (mix.getReadPointer (0), back.data(), n);
    require (err < 1.0e-4f, "STFT round trip error " + std::to_string (err));
}

// Bins against a direct DFT of the zero-extended, reflect-padded signal, as torch.stft
// (center=True, onesided) computes it.
void stftMatchesDirectDft (bool hann, bool normalized, int signalSamples)
{
    const int nFft = 512, hop = 128, half = nFft / 2;
    StemStft stft (nFft, hop, hann, normalized, signalSamples);
    const int n = signalSamples - 100;
    auto mix = makeMix (n);

    std::vector<float> re (stft.getPlaneSize()), im (stft.getPlaneSize());
    stft.forward (mix.getReadPointer (1), n, re.data(), im.data());

    std::vector<double> x ((size_t) signalSamples, 0.0);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = mix.getSample (1, i);
    auto sampleAt = [&] (int j) -> double
    {
        if (j < 0)
            j = -j;
        if (j >= signalSamples)
            j = 2 * (signalSamples - 1) - j;
        return x[(size_t) j];
    };

    const int bins = stft.getNumBins(), frames = stft.getNumFrames();
    for (const int frame : { 0, 7, frames - 1 })
    {
        for (const int bin : { 0, 3, 57, bins - 1 })
        {
            std::complex<double> sum = 0.0;
            for (int i = 0; i < nFft; ++i)
            {
                const double w = hann ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / nFft) : 1.0;
                sum += sampleAt (frame * hop + i - half) * w
                     * std::polar (1.0, -juce::MathConstants<double>::twoPi * bin * i / nFft);
            }
            if (normalized)
                sum /= std::sqrt ((double) nFft);

            const float r = re[(size_t) (bin * frames + frame)];
            const float m = im[(size_t) (bin * frames + frame)];
            require (std::abs (r - sum.real()) < 2.0e-3 && std::abs (m - sum.imag()) < 2.0e-3,
                     "bin " + std::to_string (bin) + " frame " + std::to_string (frame) + " differs from DFT");
        }
    }
}

void segmentsReconstructAnIdentityModel (int length)
{
    const int segment = 4410, stride = segment / 4;
    auto mix = makeMix (length);
    std::vector<juce::AudioBuffer<float>> results;
    const bool ok = runStemSegments (mix, segment, stride, 2,
        [] (const juce::AudioBuffer<float>& seg, std::vector<juce::AudioBuffer<float>>& outputs, int, int)
        {
            outputs[0].makeCopyOf (seg);
            outputs[1].makeCopyOf (seg);
            outputs[1].applyGain (0.5f);
            return true;
        },
        results);

    require (ok && results.size() == 2, "segmenting failed");
    for (int ch = 0; ch < 2; ++ch)
    {
        require (results[0].getNumSamples() == length, "result length");
        const auto err = maxAbsDiff (mix.getReadPointer (ch), results[0].getReadPointer (ch), length);
        require (err < 1.0e-5f, "identity reconstruction error " + std::to_string (err)
                                    + " at length " + std::to_string (length));
        require (std::abs (results[1].getSample (ch, length / 2) - 0.5f * mix.getSample (ch, length / 2)) < 1.0e-5f,
                 "second output scaled");
    }
}

juce::var parseJson (const char* text)
{
    return juce::JSON::parse (juce::String (text));
}

void latestSeriesManifestIsPicked()
{
    const auto releases = parseJson (R"([
        { "tag_name": "v0.3.0", "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.3.0/m.json" } ] },
        { "tag_name": "v0.2.12", "prerelease": true,
          "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.2.12/m.json" } ] },
        { "tag_name": "v0.2.11", "draft": true,
          "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.2.11/m.json" } ] },
        { "tag_name": "v0.2.9", "assets": [ { "name": "other.onnx", "browser_download_url": "https://x/v0.2.9/o" } ] },
        { "tag_name": "v0.2.x", "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.2.x/m.json" } ] },
        { "tag_name": "v0.20.0", "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.20.0/m.json" } ] },
        { "tag_name": "v0.2.10", "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.2.10/m.json" } ] },
        { "tag_name": "v0.2.2", "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.2.2/m.json" } ] },
        { "tag_name": "v0.2.0", "assets": [ { "name": "m.json", "browser_download_url": "https://x/v0.2.0/m.json" } ] }
    ])");

    // Numeric, not string, order (10 > 2); skips other series, prereleases, drafts, missing assets, bad tags.
    require (pickLatestSeriesManifestUrl (releases, "0.2", "m.json") == "https://x/v0.2.10/m.json", "newest v0.2.N");
    require (pickLatestSeriesManifestUrl (releases, "0.3", "m.json") == "https://x/v0.3.0/m.json", "other series");
    require (pickLatestSeriesManifestUrl (releases, "0.4", "m.json").isEmpty(), "no release in series");
    require (pickLatestSeriesManifestUrl (parseJson (R"({ "message": "API rate limit exceeded" })"), "0.2", "m.json").isEmpty(),
             "API error object");

    // This build's own configuration resolves through the same rules.
    require (getStemModelManifestDownloadUrl().endsWith ("/v0.2.1/" + getStemModelManifestAssetName()), "baseline manifest URL");
    require (getStemModelManifestFileName() == "intersect_stem_models_manifest-v0.2.json", "series-scoped cache name");
}

void manifestEntriesParse()
{
    // String stems, legacy shape (no kind): waveform.
    auto sw = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "bs-roformer-sw-6stem",
        "artifacts": { "release_asset_name": "a.onnx", "release_asset_url": "https://x/a.onnx" },
        "runtime": { "stems": ["bass","drums","other","vocals","guitar","piano"], "chunk_size_samples": 131072 }
    })"), {});
    require (sw.isValid() && sw.getNumOutputs() == 6 && sw.runtimeKind == StemRuntimeKind::waveform, "string stems");
    require (sw.outputs[0].label == "Bass", "derived label");

    // Object stems with groups, spectral runtime, checksum.
    auto sc = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "SCNet",
        "artifacts": { "release_asset_name": "scnet.onnx", "release_asset_url": "https://x/scnet.onnx",
                       "release_asset_bytes": 12900000,
                       "sha256": { "onnx": "98228931494151762A1C4AB1EC7899A894B1F81FD4A509921A9D2F9BACC50845" } },
        "runtime": { "kind": "spectral",
                     "stems": [ { "name": "drums", "group": "Rhythm" }, "bass", "other", { "name": "lead-vocal", "label": "Lead" } ],
                     "stem_groups": [ "Vocals", "Rhythm" ],
                     "stft": { "n_fft": 4096, "hop_length": 1024, "window": "rect", "normalized": true,
                               "frames": 476, "segment_samples": 485100, "stride_samples": 121275 } }
    })"), {});
    require (sc.isValid() && sc.id == "scnet", "spectral entry with normalised id");
    require (sc.runtimeKind == StemRuntimeKind::spectral && sc.spectral.frames == 476 && ! sc.spectral.hannWindow, "stft params");
    require (sc.outputs[0].group == "Rhythm" && sc.outputs[3].label == "Lead", "stem objects");
    require (sc.groupOrder.size() == 2 && sc.groupOrder[1] == "Rhythm", "group order");
    require (getStemSpectralSignalSamples (sc.spectral) == 486400, "SCNet pads its segment to 486400");

    auto mini = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "mini", "artifacts": { "release_asset_name": "m.onnx" },
        "runtime": { "kind": "spectral_mask", "stems": ["bass","drums","other","vocals"],
                     "stft": { "n_fft": 4096, "hop_length": 512, "window": "hann", "normalized": false,
                               "frames": 690, "segment_samples": 352800, "stride_samples": 176400,
                               "mask_n_fft": 2048, "normalize_input": false } } })"), {});
    require (mini.isValid() && mini.runtimeKind == StemRuntimeKind::spectralMask && mini.spectral.maskNFft == 2048
             && ! mini.spectral.normalizeInput && getStemSpectralSignalSamples (mini.spectral) == 352800, "mask entry");

    auto badFrames = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "bad", "artifacts": { "release_asset_name": "b.onnx" },
        "runtime": { "kind": "spectral", "stems": ["a"],
                     "stft": { "n_fft": 4096, "hop_length": 1024, "frames": 400, "segment_samples": 485100, "stride_samples": 1000 } } })"), {});
    require (! badFrames.isValid(), "frame count that doesn't match the segment is rejected");
    require (sc.sha256 == "98228931494151762a1c4ab1ec7899a894b1f81fd4a509921a9d2f9bacc50845", "lowercased sha");
    require (sc.downloadBytes == 12900000, "size");

    // Unknown runtime kind (a newer manifest) is skipped.
    auto future = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "x", "artifacts": { "release_asset_name": "x.onnx" },
        "runtime": { "kind": "diffusion", "stems": ["a"] } })"), {});
    require (! future.isValid(), "unknown runtime kind rejected");

    // Many stems: no 6-stem cap.
    juce::String many = R"({ "model_id": "mega", "artifacts": { "release_asset_name": "m.onnx" }, "runtime": { "stems": [)";
    for (int i = 0; i < 53; ++i)
        many << (i > 0 ? "," : "") << "\"s" << i << "\"";
    many << "] } }";
    auto mega = parseStemModelManifestEntry (juce::JSON::parse (many), {});
    require (mega.isValid() && mega.getNumOutputs() == 53, "53 stems");

    // Override keeps built-in fields the manifest doesn't mention.
    const auto& builtIn = getBuiltInStemModelCatalog().front();
    auto overridden = parseStemModelManifestEntry (parseJson (R"({ "model_id": "bs-roformer-sw-6stem", "name": "Renamed" })"), builtIn);
    require (overridden.isValid() && overridden.menuLabel == "Renamed" && overridden.getNumOutputs() == 6, "override");
    require (overridden.requirements.ramGb == builtIn.requirements.ramGb, "override keeps requirements");

    // Requirements: each field optional, so a partial block only replaces what it names.
    auto withReq = parseStemModelManifestEntry (parseJson (R"({ "model_id": "bs-roformer-sw-6stem",
        "requirements": { "ram_gb": 7.5, "gpu": "optional" } })"), builtIn);
    require (withReq.requirements.ramGb == 7.5f && withReq.requirements.gpu == StemGpuAdvice::optional
                 && withReq.requirements.cpuRealtimeFactor == builtIn.requirements.cpuRealtimeFactor,
             "partial requirements");
}

void selectionAndNames()
{
    auto sel = normaliseStemSelection ({ 5, 1, 1, -2, 9, 3 }, 6);
    require ((sel == StemSelection { 1, 3, 5 }), "selection normalised");

    require (normaliseStemModelId ("bsroformer4stem_fp32") == getDefaultStemModelId(), "legacy id");
    require (stemOutputLabelFromName ("bowed_strings") == "Bowed Strings", "label from name");

    StemMetadata meta;
    meta.setName ("lead-vocal");
    require (meta.getName() == "lead-vocal", "name round trip");
    meta.setName (juce::String::repeatedString ("x", 100));
    require (meta.getName().length() == StemMetadata::kMaxNameBytes - 1, "name truncated");
}
void builtInCatalog()
{
    for (const auto& entry : getBuiltInStemModelCatalog())
    {
        require (entry.isValid() && entry.sha256.length() == 64 && entry.downloadBytes > 0, "built-in entry valid");
        require (entry.notes.isNotEmpty() && entry.requirements.ramGb > 0.0f && entry.requirements.cpuRealtimeFactor > 0.0f
                     && entry.requirements.gpu != StemGpuAdvice::unknown,
                 "built-in entry has a description and requirements");
        for (const auto& output : entry.outputs)
            require (output.group.isEmpty() || entry.groupOrder.contains (output.group), "group is in groupOrder");
    }

    const auto& catalog = getBuiltInStemModelCatalog();
    const auto megaIt = std::find_if (catalog.begin(), catalog.end(), [] (const auto& e) { return e.id == "mvsep-mega-53"; });
    require (megaIt != catalog.end() && megaIt->getNumOutputs() == 53, "Mega 53 built in");
    const auto& mega = *megaIt;
    require (mega.outputs[0].name == "accordion" && mega.outputs[52].name == "woodwind", "Mega 53 stem order");
    const auto hh = std::find_if (mega.outputs.begin(), mega.outputs.end(), [] (const auto& o) { return o.name == "hh"; });
    require (hh != mega.outputs.end() && hh->label == "Hi-Hat" && hh->group == "Drums", "hh label");
}
} // namespace

void gpuFailuresAreExplained()
{
    const auto tdr = describeStemGpuFailure ("Mini", "Non-zero status code returned while running DmlFusedNode_0_0 node. "
                                                     "... Exception(4) tid(2ac4) 887A0006 The GPU will not respond to more commands");
    require (tdr.startsWith ("Windows reset the graphics driver") && tdr.contains ("Mini") && tdr.contains ("887A0006"),
             "887A0006 is a driver reset, and the details are kept");
    require (describeStemGpuFailure ("M", "887a0005").startsWith ("Windows reset"), "device removed");
    require (describeStemGpuFailure ("M", "8007000E Not enough memory resources").contains ("ran out of graphics memory"), "out of memory");
    require (describeStemGpuFailure ("M", "failed to allocate 2 GB").contains ("ran out of graphics memory"), "allocation failure");
    require (describeStemGpuFailure ("M", "80070057 The parameter is incorrect").contains ("rejected"), "rejected model");
    require (describeStemGpuFailure ("M", "887A0020").contains ("internal error"), "driver internal error");
    require (describeStemGpuFailure ("M", "something else").startsWith ("The GPU couldn't run M"), "unknown error");
}

void suspectOutputIsCaught()
{
    std::vector<float> zeros (1000, 0.0f), audio (1000, 0.0f);
    for (int i = 0; i < 1000; ++i)
        audio[(size_t) i] = 0.1f * std::sin (0.05f * (float) i);

    require (isSuspectStemModelOutput (zeros.data(), zeros.size(), 0.5f), "zeros for loud input (driver reset)");
    require (! isSuspectStemModelOutput (zeros.data(), zeros.size(), 0.0f), "zeros for silent input are fine");
    require (! isSuspectStemModelOutput (audio.data(), audio.size(), 0.5f), "real output is fine");
    audio[500] = std::numeric_limits<float>::quiet_NaN();
    require (isSuspectStemModelOutput (audio.data(), audio.size(), 0.5f), "NaN");
    audio[500] = std::numeric_limits<float>::infinity();
    require (isSuspectStemModelOutput (audio.data(), audio.size(), 0.0f), "Inf");
}

void modelUpdatesResolve()
{
    auto entry = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "mini",
        "artifacts": { "release_asset_name": "mini2.onnx", "release_asset_url": "https://x/mini2.onnx",
                       "replaces": [ "mini1.onnx", "mini2.onnx", "../evil.onnx", "sub/dir.onnx", "C:x.onnx", "", "mini0.onnx" ],
                       "update_note": "  Fixes GPU crashes  " },
        "runtime": { "stems": ["a", "b"], "chunk_size_samples": 1024 }
    })"), {});
    require (entry.isValid(), "entry with replaces parses");
    require (entry.replacesFileNames.size() == 2 && entry.replacesFileNames[0] == "mini1.onnx"
                 && entry.replacesFileNames[1] == "mini0.onnx",
             "only bare file names other than the current file are kept");
    require (entry.updateNote == "Fixes GPU crashes", "update note trimmed");

    const auto older = parseStemModelManifestEntry (parseJson (R"({
        "model_id": "mini", "artifacts": { "release_asset_name": "mini1.onnx" },
        "runtime": { "stems": ["a", "b"], "chunk_size_samples": 1024 }
    })"), {});
    require (older.replacesFileNames.isEmpty() && older.updateNote.isEmpty(), "fields are optional");

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("intersect-stem-update-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    require (folder.createDirectory(), "temp folder");
    const auto current = folder.getChildFile ("mini2.onnx");
    const auto earlier = folder.getChildFile ("mini1.onnx");
    const auto unrelated = folder.getChildFile ("other.onnx");

    require (findInstalledStemModelFile (entry, folder) == current && ! isStemModelUpdateAvailable (entry, folder),
             "nothing installed: no update, current path");

    require (earlier.replaceWithText ("old") && unrelated.replaceWithText ("keep"), "write files");
    require (findInstalledStemModelFile (entry, folder) == earlier, "old version is used until the update");
    require (isStemModelUpdateAvailable (entry, folder), "old version only: update available");

    require (current.replaceWithText ("new"), "write new file");
    require (findInstalledStemModelFile (entry, folder) == current && ! isStemModelUpdateAvailable (entry, folder),
             "new version wins once installed");

    require (removeReplacedStemModelFiles (entry, folder) == 1, "old version deleted");
    require (! earlier.existsAsFile() && current.existsAsFile() && unrelated.existsAsFile(),
             "only listed earlier versions are deleted");
    require (removeReplacedStemModelFiles (entry, folder) == 0, "nothing left to delete");

    (void) folder.deleteRecursively();
}

int main()
{
    int failed = 0;
    int passed = 0;
    const auto run = [&] (const char* name, auto test)
    {
        try
        {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    // 39 * 128: SCNet-style (signal a multiple of the hop); 5000: Mini-style (it isn't).
    run ("STFT round trip (rectangular, normalized)", [] { stftRoundTripIsIdentity (false, true, 39 * 128); });
    run ("STFT round trip (Hann)", [] { stftRoundTripIsIdentity (true, false, 5000); });
    run ("STFT matches direct DFT (rectangular, normalized)", [] { stftMatchesDirectDft (false, true, 39 * 128); });
    run ("STFT matches direct DFT (Hann)", [] { stftMatchesDirectDft (true, false, 5000); });
    run ("segments reconstruct (long)", [] { segmentsReconstructAnIdentityModel (44100 * 3 + 17); });
    run ("segments reconstruct (short)", [] { segmentsReconstructAnIdentityModel (3000); });
    run ("segments reconstruct (exactly one)", [] { segmentsReconstructAnIdentityModel (4410); });
    run ("manifest entries", manifestEntriesParse);
    run ("latest series manifest", latestSeriesManifestIsPicked);
    run ("selection and names", selectionAndNames);
    run ("built-in catalog", builtInCatalog);
    run ("GPU failures are explained", gpuFailuresAreExplained);
    run ("suspect GPU output is caught", suspectOutputIsCaught);
    run ("model updates resolve", modelUpdatesResolve);

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
