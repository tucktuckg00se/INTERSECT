#include "StemCoreML.h"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

#include <cstring>
#include <stdexcept>

#if ! __has_feature(objc_arc)
 #error "StemCoreML.mm must be compiled with -fobjc-arc"
#endif

namespace
{
juce::String describe (NSError* error)
{
    return error != nil ? juce::String::fromUTF8 ([[error localizedDescription] UTF8String]) : juce::String ("unknown error");
}

NSURL* toUrl (const juce::File& file)
{
    NSString* path = [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()];
    return [NSURL fileURLWithPath: path != nil ? path : @""];
}

// The compiled model for this exact package: <cacheRoot>/<package>-<weights size>-<manifest time>.mlmodelc.
juce::File getCompiledModelFolder (const juce::File& package, const juce::File& cacheRoot)
{
    const auto weights = package.getChildFile ("Data/com.apple.CoreML/weights/weight.bin");
    return cacheRoot.getChildFile (package.getFileNameWithoutExtension() + "-" + juce::String (weights.getSize()) + "-"
                                   + juce::String (package.getChildFile ("Manifest.json").getLastModificationTime().toMilliseconds())
                                   + ".mlmodelc");
}

void removeOtherCompiledVersions (const juce::File& package, const juce::File& cacheRoot, const juce::File& keep)
{
    const auto prefix = package.getFileNameWithoutExtension() + "-";
    for (const auto& folder : cacheRoot.findChildFiles (juce::File::findDirectories, false))
        if (folder != keep && folder.getFileName().startsWith (prefix))
            (void) folder.deleteRecursively();
}

// Compiles the package and moves the result into place under a temporary name first, so a compile cut short
// (quit, crash) is never mistaken for a finished one.
API_AVAILABLE (macos (13.0))
bool compileInto (const juce::File& package, const juce::File& compiled, juce::String& error)
{
    __block NSURL* compiledUrl = nil;
    __block NSError* compileError = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create (0);
    [MLModel compileModelAtURL: toUrl (package)
             completionHandler: ^(NSURL* url, NSError* err)
             {
                 compiledUrl = url;
                 compileError = err;
                 dispatch_semaphore_signal (done);
             }];
    dispatch_semaphore_wait (done, DISPATCH_TIME_FOREVER);

    if (compiledUrl == nil)
    {
        error = "couldn't compile the Core ML model: " + describe (compileError);
        return false;
    }

    const auto partial = compiled.getSiblingFile (compiled.getFileName() + ".partial");
    (void) partial.deleteRecursively();
    NSFileManager* files = [NSFileManager defaultManager];
    NSError* moveError = nil;
    if (! [files moveItemAtURL: compiledUrl toURL: toUrl (partial) error: &moveError]
        && ! [files copyItemAtURL: compiledUrl toURL: toUrl (partial) error: &moveError])
    {
        error = "couldn't store the compiled Core ML model: " + describe (moveError);
        return false;
    }
    (void) [files removeItemAtURL: compiledUrl error: nil];

    (void) compiled.deleteRecursively();
    if (! partial.moveFileTo (compiled))
    {
        error = "couldn't store the compiled Core ML model in " + compiled.getParentDirectory().getFullPathName();
        return false;
    }
    return true;
}

API_AVAILABLE (macos (13.0))
MLModel* loadCompiled (const juce::File& compiled, juce::String& error)
{
    MLModelConfiguration* config = [[MLModelConfiguration alloc] init];
    config.computeUnits = MLComputeUnitsCPUAndGPU;
    NSError* loadError = nil;
    MLModel* model = [MLModel modelWithContentsOfURL: toUrl (compiled) configuration: config error: &loadError];
    if (model == nil)
        error = "couldn't load the Core ML model: " + describe (loadError);
    return model;
}

class API_AVAILABLE (macos (13.0)) CoreMlStemRunner final : public StemModelRunner
{
public:
    CoreMlStemRunner (MLModel* m, NSString* in, NSString* out) : model (m), inputName (in), outputName (out) {}

    void run (const float* input, size_t inputCount, const std::vector<int64_t>& inputShape,
              const float*& output, std::vector<int64_t>& outputShape) override
    {
        @autoreleasepool
        {
            NSMutableArray<NSNumber*>* shape = [NSMutableArray arrayWithCapacity: inputShape.size()];
            NSMutableArray<NSNumber*>* strides = [NSMutableArray arrayWithCapacity: inputShape.size()];
            int64_t stride = 1;
            for (size_t i = inputShape.size(); i-- > 0;)
            {
                [shape insertObject: @(inputShape[i]) atIndex: 0];
                [strides insertObject: @(stride) atIndex: 0];
                stride *= inputShape[i];
            }
            if ((size_t) stride != inputCount)
                throw std::runtime_error ("Core ML input size doesn't match its shape");

            NSError* error = nil;
            // Wraps the caller's buffer without copying; Core ML only reads it during the prediction.
            MLMultiArray* array = [[MLMultiArray alloc] initWithDataPointer: const_cast<float*> (input)
                                                                       shape: shape
                                                                    dataType: MLMultiArrayDataTypeFloat32
                                                                     strides: strides
                                                                 deallocator: nil
                                                                       error: &error];
            if (array == nil)
                throw std::runtime_error (("Core ML input: " + describe (error)).toStdString());

            MLDictionaryFeatureProvider* features = [[MLDictionaryFeatureProvider alloc]
                initWithDictionary: @{ inputName: [MLFeatureValue featureValueWithMultiArray: array] }
                             error: &error];
            if (features == nil)
                throw std::runtime_error (("Core ML input: " + describe (error)).toStdString());

            id<MLFeatureProvider> result = [model predictionFromFeatures: features error: &error];
            if (result == nil)
                throw std::runtime_error (("Core ML prediction failed: " + describe (error)).toStdString());

            MLMultiArray* out = [result featureValueForName: outputName].multiArrayValue;
            if (out == nil)
                throw std::runtime_error ("Core ML returned no output tensor");

            copyOut (out, outputShape);
        }
        output = buffer.data();
    }

private:
    void copyOut (MLMultiArray* out, std::vector<int64_t>& outputShape)
    {
        const auto rank = (size_t) out.shape.count;
        outputShape.resize (rank);
        std::vector<int64_t> strides (rank);
        size_t count = 1;
        for (size_t i = 0; i < rank; ++i)
        {
            outputShape[i] = out.shape[i].longLongValue;
            strides[i] = out.strides[i].longLongValue;
            count *= (size_t) juce::jmax<int64_t> (0, outputShape[i]);
        }
        buffer.resize (count);

        const auto type = out.dataType;
        if (type != MLMultiArrayDataTypeFloat32 && type != MLMultiArrayDataTypeFloat16 && type != MLMultiArrayDataTypeDouble)
            throw std::runtime_error ("Core ML returned an unsupported output type");

        float* dst = buffer.data();
        const std::vector<int64_t> dims = outputShape;   // blocks copy what they capture; no C++ references
        [out getBytesWithHandler: ^(const void* bytes, NSInteger)
        {
            // Walks every element in row-major order, reading through the array's own strides (which may be padded).
            std::vector<int64_t> index (rank, 0);
            for (size_t n = 0; n < count; ++n)
            {
                int64_t offset = 0;
                for (size_t d = 0; d < rank; ++d)
                    offset += index[d] * strides[d];
                if (type == MLMultiArrayDataTypeFloat32)
                    dst[n] = static_cast<const float*> (bytes)[offset];
                else if (type == MLMultiArrayDataTypeFloat16)
                    dst[n] = (float) static_cast<const __fp16*> (bytes)[offset];
                else
                    dst[n] = (float) static_cast<const double*> (bytes)[offset];

                for (size_t d = rank; d-- > 0;)
                {
                    if (++index[d] < dims[d])
                        break;
                    index[d] = 0;
                }
            }
        }];
    }

    MLModel* model;
    NSString* inputName;
    NSString* outputName;
    std::vector<float> buffer;
};
} // namespace

std::unique_ptr<StemModelRunner> createCoreMlStemRunner (const juce::File& package, const juce::File& cacheRoot,
                                                         juce::String& error)
{
    if (@available (macOS 13.0, *))
    {
        if (! package.getChildFile ("Manifest.json").existsAsFile())
        {
            error = "the Core ML model at " + package.getFullPathName() + " is missing or damaged";
            return nullptr;
        }
        if (cacheRoot.createDirectory().failed())
        {
            error = "couldn't create " + cacheRoot.getFullPathName();
            return nullptr;
        }

        const auto compiled = getCompiledModelFolder (package, cacheRoot);
        MLModel* model = nil;
        if (compiled.isDirectory())
        {
            juce::String ignored;
            model = loadCompiled (compiled, ignored);   // a broken cache entry is compiled again below
        }
        if (model == nil)
        {
            removeOtherCompiledVersions (package, cacheRoot, {});
            if (! compileInto (package, compiled, error))
                return nullptr;
            model = loadCompiled (compiled, error);
            if (model == nil)
                return nullptr;
        }
        removeOtherCompiledVersions (package, cacheRoot, compiled);

        MLModelDescription* description = model.modelDescription;
        NSString* inputName = description.inputDescriptionsByName.allKeys.firstObject;
        NSString* outputName = description.outputDescriptionsByName.allKeys.firstObject;
        if (inputName == nil || outputName == nil || description.inputDescriptionsByName.count != 1)
        {
            error = "the Core ML model doesn't have exactly one input";
            return nullptr;
        }
        return std::make_unique<CoreMlStemRunner> (model, inputName, outputName);
    }

    error = "Core ML stem models need macOS 13 or newer";
    return nullptr;
}
