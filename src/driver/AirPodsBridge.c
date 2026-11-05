// AirPods Bridge — minimal 2ch/48k loopback AudioServerPlugIn (output -> input)
#include <CoreAudio/AudioServerPlugIn.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <string.h>

enum { kObj_PlugIn = kAudioObjectPlugInObject, kObj_Device = 2, kObj_In = 3, kObj_Out = 4 };
#define kRate 48000.0
#define kCh 2
#define kRing 16384
#define kDevUID "AirPodsBridge_UID"

static AudioServerPlugInHostRef gHost;
static UInt32 gRef = 1;
static pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;
static UInt64 gIOCount = 0, gAnchor = 0, gNumTS = 0;
static Float64 gTicksPerFrame = 0;
static float gRing[kRing * kCh];
static UInt32 gInActive = 1, gOutActive = 1;

static HRESULT QI(void*, REFIID, LPVOID*);
static ULONG AddRef(void*); static ULONG Release(void*);
static OSStatus Initialize(AudioServerPlugInDriverRef, AudioServerPlugInHostRef);
static OSStatus CreateDevice(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*, AudioObjectID*);
static OSStatus DestroyDevice(AudioServerPlugInDriverRef, AudioObjectID);
static OSStatus AddClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
static OSStatus RemoveClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
static OSStatus PerformCfg(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
static OSStatus AbortCfg(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
static Boolean HasProperty(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*);
static OSStatus IsSettable(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, Boolean*);
static OSStatus GetSize(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
static OSStatus GetData(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, UInt32*, void*);
static OSStatus SetData(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, const void*);
static OSStatus StartIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
static OSStatus StopIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
static OSStatus ZeroTS(AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64*, UInt64*, UInt64*);
static OSStatus WillDo(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, Boolean*, Boolean*);
static OSStatus BeginIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*);
static OSStatus DoIO(AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*, void*, void*);
static OSStatus EndIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*);

static AudioServerPlugInDriverInterface gIface = { NULL, QI, AddRef, Release, Initialize, CreateDevice, DestroyDevice,
  AddClient, RemoveClient, PerformCfg, AbortCfg, HasProperty, IsSettable, GetSize, GetData, SetData,
  StartIO, StopIO, ZeroTS, WillDo, BeginIO, DoIO, EndIO };
static AudioServerPlugInDriverInterface* gIfacePtr = &gIface;
static AudioServerPlugInDriverRef gDriver = &gIfacePtr;

void* AirPodsBridge_Create(CFAllocatorRef a, CFUUIDRef type) {
  (void)a; return CFEqual(type, kAudioServerPlugInTypeUUID) ? gDriver : NULL;
}
static HRESULT QI(void* d, REFIID uuid, LPVOID* out) {
  (void)d; CFUUIDRef r = CFUUIDCreateFromUUIDBytes(NULL, uuid); HRESULT res = E_NOINTERFACE;
  if (CFEqual(r, IUnknownUUID) || CFEqual(r, kAudioServerPlugInDriverInterfaceUUID)) { gRef++; *out = gDriver; res = S_OK; }
  CFRelease(r); return res;
}
static ULONG AddRef(void* d) { (void)d; return ++gRef; }
static ULONG Release(void* d) { (void)d; if (gRef > 0) gRef--; return gRef; }
static OSStatus Initialize(AudioServerPlugInDriverRef d, AudioServerPlugInHostRef h) {
  (void)d; gHost = h; mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  gTicksPerFrame = ((Float64)tb.denom / (Float64)tb.numer) * 1e9 / kRate; return 0;
}
static OSStatus CreateDevice(AudioServerPlugInDriverRef d, CFDictionaryRef x, const AudioServerPlugInClientInfo* c, AudioObjectID* o) { return kAudioHardwareUnsupportedOperationError; }
static OSStatus DestroyDevice(AudioServerPlugInDriverRef d, AudioObjectID o) { return kAudioHardwareUnsupportedOperationError; }
static OSStatus AddClient(AudioServerPlugInDriverRef d, AudioObjectID o, const AudioServerPlugInClientInfo* c) { return 0; }
static OSStatus RemoveClient(AudioServerPlugInDriverRef d, AudioObjectID o, const AudioServerPlugInClientInfo* c) { return 0; }
static OSStatus PerformCfg(AudioServerPlugInDriverRef d, AudioObjectID o, UInt64 a, void* i) { return 0; }
static OSStatus AbortCfg(AudioServerPlugInDriverRef d, AudioObjectID o, UInt64 a, void* i) { return 0; }

static AudioStreamBasicDescription Fmt(void) {
  AudioStreamBasicDescription f; memset(&f, 0, sizeof f);
  f.mSampleRate = kRate; f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
  f.mBytesPerPacket = 8; f.mFramesPerPacket = 1; f.mBytesPerFrame = 8; f.mChannelsPerFrame = kCh; f.mBitsPerChannel = 32;
  return f;
}
#define PUT(T, v) do { if (sz < sizeof(T)) return kAudioHardwareBadPropertySizeError; *(T*)out = (v); *outSz = sizeof(T); return 0; } while (0)
static OSStatus PutIDs(const AudioObjectID* ids, UInt32 n, UInt32 sz, UInt32* outSz, void* out) {
  UInt32 m = sz / sizeof(AudioObjectID); if (m > n) m = n;
  memcpy(out, ids, m * sizeof(AudioObjectID)); *outSz = m * sizeof(AudioObjectID); return 0;
}

static OSStatus GetData(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a,
                        UInt32 qs, const void* q, UInt32 sz, UInt32* outSz, void* out) {
  (void)d; (void)pid; (void)qs;
  AudioObjectPropertySelector s = a->mSelector;
  if (obj == kObj_PlugIn) {
    switch (s) {
      case kAudioObjectPropertyBaseClass: PUT(AudioClassID, kAudioObjectClassID);
      case kAudioObjectPropertyClass: PUT(AudioClassID, kAudioPlugInClassID);
      case kAudioObjectPropertyOwner: PUT(AudioObjectID, kAudioObjectUnknown);
      case kAudioObjectPropertyManufacturer: PUT(CFStringRef, CFSTR("RAPSODICA"));
      case kAudioObjectPropertyOwnedObjects:
      case kAudioPlugInPropertyDeviceList: { AudioObjectID i = kObj_Device; return PutIDs(&i, 1, sz, outSz, out); }
      case kAudioPlugInPropertyTranslateUIDToDevice: {
        CFStringRef uid = q ? *(CFStringRef*)q : NULL;
        PUT(AudioObjectID, (uid && CFStringCompare(uid, CFSTR(kDevUID), 0) == kCFCompareEqualTo) ? kObj_Device : kAudioObjectUnknown); }
      case kAudioPlugInPropertyResourceBundle: PUT(CFStringRef, CFSTR(""));
    }
  } else if (obj == kObj_Device) {
    switch (s) {
      case kAudioObjectPropertyBaseClass: PUT(AudioClassID, kAudioObjectClassID);
      case kAudioObjectPropertyClass: PUT(AudioClassID, kAudioDeviceClassID);
      case kAudioObjectPropertyOwner: PUT(AudioObjectID, kObj_PlugIn);
      case kAudioObjectPropertyName: PUT(CFStringRef, CFSTR("AirPods Bridge"));
      case kAudioObjectPropertyManufacturer: PUT(CFStringRef, CFSTR("RAPSODICA"));
      case kAudioDevicePropertyDeviceUID: PUT(CFStringRef, CFSTR(kDevUID));
      case kAudioDevicePropertyModelUID: PUT(CFStringRef, CFSTR("AirPodsBridge_Model"));
      case kAudioObjectPropertyOwnedObjects:
      case kAudioDevicePropertyStreams: {
        AudioObjectID all[2] = { kObj_In, kObj_Out }, in = kObj_In, o = kObj_Out;
        if (a->mScope == kAudioObjectPropertyScopeInput) return PutIDs(&in, 1, sz, outSz, out);
        if (a->mScope == kAudioObjectPropertyScopeOutput) return PutIDs(&o, 1, sz, outSz, out);
        return PutIDs(all, 2, sz, outSz, out); }
      case kAudioObjectPropertyControlList: *outSz = 0; return 0;
      case kAudioDevicePropertyTransportType: PUT(UInt32, kAudioDeviceTransportTypeVirtual);
      case kAudioDevicePropertyRelatedDevices: { AudioObjectID i = kObj_Device; return PutIDs(&i, 1, sz, outSz, out); }
      case kAudioDevicePropertyClockDomain: PUT(UInt32, 0);
      case kAudioDevicePropertyDeviceIsAlive: PUT(UInt32, 1);
      case kAudioDevicePropertyDeviceIsRunning: PUT(UInt32, gIOCount > 0 ? 1 : 0);
      case kAudioDevicePropertyDeviceCanBeDefaultDevice: PUT(UInt32, 1);
      case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: PUT(UInt32, 0);
      case kAudioDevicePropertyLatency: PUT(UInt32, 0);
      case kAudioDevicePropertySafetyOffset: PUT(UInt32, 0);
      case kAudioDevicePropertyNominalSampleRate: PUT(Float64, kRate);
      case kAudioDevicePropertyAvailableNominalSampleRates: { AudioValueRange r = { kRate, kRate }; PUT(AudioValueRange, r); }
      case kAudioDevicePropertyIsHidden: PUT(UInt32, 0);
      case kAudioDevicePropertyPreferredChannelsForStereo: {
        if (sz < 8) return kAudioHardwareBadPropertySizeError;
        ((UInt32*)out)[0] = 1; ((UInt32*)out)[1] = 2; *outSz = 8; return 0; }
      case kAudioDevicePropertyZeroTimeStampPeriod: PUT(UInt32, kRing);
    }
  } else if (obj == kObj_In || obj == kObj_Out) {
    int isIn = (obj == kObj_In);
    switch (s) {
      case kAudioObjectPropertyBaseClass: PUT(AudioClassID, kAudioObjectClassID);
      case kAudioObjectPropertyClass: PUT(AudioClassID, kAudioStreamClassID);
      case kAudioObjectPropertyOwner: PUT(AudioObjectID, kObj_Device);
      case kAudioObjectPropertyOwnedObjects: *outSz = 0; return 0;
      case kAudioStreamPropertyIsActive: PUT(UInt32, isIn ? gInActive : gOutActive);
      case kAudioStreamPropertyDirection: PUT(UInt32, isIn ? 1 : 0);
      case kAudioStreamPropertyTerminalType: PUT(UInt32, isIn ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker);
      case kAudioStreamPropertyStartingChannel: PUT(UInt32, 1);
      case kAudioStreamPropertyLatency: PUT(UInt32, 0);
      case kAudioStreamPropertyVirtualFormat:
      case kAudioStreamPropertyPhysicalFormat: PUT(AudioStreamBasicDescription, Fmt());
      case kAudioStreamPropertyAvailableVirtualFormats:
      case kAudioStreamPropertyAvailablePhysicalFormats: {
        AudioStreamRangedDescription r; r.mFormat = Fmt(); r.mSampleRateRange.mMinimum = kRate; r.mSampleRateRange.mMaximum = kRate;
        PUT(AudioStreamRangedDescription, r); }
    }
  }
  return kAudioHardwareUnknownPropertyError;
}
static OSStatus GetSize(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, UInt32 qs, const void* q, UInt32* outSz) {
  char buf[512]; OSStatus st = GetData(d, obj, pid, a, qs, q, sizeof buf, outSz, buf);
  if (st == 0 && a->mSelector == kAudioPlugInPropertyTranslateUIDToDevice) *outSz = sizeof(AudioObjectID);
  return st;
}
static Boolean HasProperty(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a) {
  UInt32 sz; CFStringRef dummy = CFSTR(""); return GetSize(d, obj, pid, a, sizeof dummy, &dummy, &sz) == 0;
}
static OSStatus IsSettable(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, Boolean* outS) {
  if (!HasProperty(d, obj, pid, a)) return kAudioHardwareUnknownPropertyError;
  AudioObjectPropertySelector s = a->mSelector;
  *outS = (obj == kObj_Device && s == kAudioDevicePropertyNominalSampleRate) ||
          ((obj == kObj_In || obj == kObj_Out) && (s == kAudioStreamPropertyIsActive || s == kAudioStreamPropertyVirtualFormat || s == kAudioStreamPropertyPhysicalFormat));
  return 0;
}
static OSStatus SetData(AudioServerPlugInDriverRef d, AudioObjectID obj, pid_t pid, const AudioObjectPropertyAddress* a, UInt32 qs, const void* q, UInt32 sz, const void* in) {
  AudioObjectPropertySelector s = a->mSelector;
  if (obj == kObj_Device && s == kAudioDevicePropertyNominalSampleRate) {
    if (sz < sizeof(Float64)) return kAudioHardwareBadPropertySizeError;
    return (*(const Float64*)in == kRate) ? 0 : kAudioDeviceUnsupportedFormatError;
  }
  if (obj == kObj_In || obj == kObj_Out) {
    if (s == kAudioStreamPropertyIsActive) { if (sz < 4) return kAudioHardwareBadPropertySizeError;
      if (obj == kObj_In) gInActive = *(const UInt32*)in; else gOutActive = *(const UInt32*)in; return 0; }
    if (s == kAudioStreamPropertyVirtualFormat || s == kAudioStreamPropertyPhysicalFormat) {
      if (sz < sizeof(AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
      const AudioStreamBasicDescription* f = in;
      return (f->mSampleRate == kRate && f->mChannelsPerFrame == kCh && f->mFormatID == kAudioFormatLinearPCM && f->mBitsPerChannel == 32) ? 0 : kAudioDeviceUnsupportedFormatError; }
  }
  return HasProperty(d, obj, pid, a) ? kAudioHardwareUnsupportedOperationError : kAudioHardwareUnknownPropertyError;
}
static OSStatus StartIO(AudioServerPlugInDriverRef d, AudioObjectID o, UInt32 c) {
  pthread_mutex_lock(&gMutex);
  if (gIOCount == 0) { gNumTS = 0; gAnchor = mach_absolute_time(); memset(gRing, 0, sizeof gRing); }
  gIOCount++; pthread_mutex_unlock(&gMutex); return 0;
}
static OSStatus StopIO(AudioServerPlugInDriverRef d, AudioObjectID o, UInt32 c) {
  pthread_mutex_lock(&gMutex); if (gIOCount > 0) gIOCount--; pthread_mutex_unlock(&gMutex); return 0;
}
static OSStatus ZeroTS(AudioServerPlugInDriverRef d, AudioObjectID o, UInt32 c, Float64* st, UInt64* ht, UInt64* seed) {
  pthread_mutex_lock(&gMutex);
  UInt64 now = mach_absolute_time();
  Float64 ticksPerRing = gTicksPerFrame * kRing;
  UInt64 next = gAnchor + (UInt64)((gNumTS + 1) * ticksPerRing);
  if (next <= now) gNumTS++;
  *st = (Float64)(gNumTS * kRing);
  *ht = gAnchor + (UInt64)(gNumTS * ticksPerRing);
  *seed = 1;
  pthread_mutex_unlock(&gMutex); return 0;
}
static OSStatus WillDo(AudioServerPlugInDriverRef d, AudioObjectID o, UInt32 c, UInt32 op, Boolean* will, Boolean* inPlace) {
  *will = (op == kAudioServerPlugInIOOperationReadInput || op == kAudioServerPlugInIOOperationWriteMix); *inPlace = 1; return 0;
}
static OSStatus BeginIO(AudioServerPlugInDriverRef d, AudioObjectID o, UInt32 c, UInt32 op, UInt32 n, const AudioServerPlugInIOCycleInfo* i) { return 0; }
static OSStatus EndIO(AudioServerPlugInDriverRef d, AudioObjectID o, UInt32 c, UInt32 op, UInt32 n, const AudioServerPlugInIOCycleInfo* i) { return 0; }
static void RingCopy(float* buf, Float64 sampleTime, UInt32 frames, int write) {
  if (sampleTime < 0) sampleTime = 0;
  UInt32 pos = (UInt32)((UInt64)sampleTime % kRing);
  UInt32 first = frames; if (pos + first > kRing) first = kRing - pos;
  float* r = gRing + pos * kCh; size_t b1 = first * kCh * sizeof(float);
  if (write) memcpy(r, buf, b1); else { memcpy(buf, r, b1); memset(r, 0, b1); }
  if (frames > first) { size_t b2 = (frames - first) * kCh * sizeof(float);
    if (write) memcpy(gRing, buf + first * kCh, b2); else { memcpy(buf + first * kCh, gRing, b2); memset(gRing, 0, b2); } }
}
static OSStatus DoIO(AudioServerPlugInDriverRef d, AudioObjectID dev, AudioObjectID stream, UInt32 c, UInt32 op, UInt32 n,
                     const AudioServerPlugInIOCycleInfo* info, void* main, void* sec) {
  if (n > kRing) n = kRing;
  if (op == kAudioServerPlugInIOOperationWriteMix && stream == kObj_Out) RingCopy(main, info->mOutputTime.mSampleTime, n, 1);
  else if (op == kAudioServerPlugInIOOperationReadInput && stream == kObj_In) RingCopy(main, info->mInputTime.mSampleTime, n, 0);
  return 0;
}
