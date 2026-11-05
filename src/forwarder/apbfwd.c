// AirPods Bridge forwarder: AirPods Bridge (input side) -> AirPods output, with drift correction.
// usage: apbfwd [seconds]      run forwarder (0/absent = forever)
//        apbfwd tone [seconds] play a quiet 440 Hz test tone INTO AirPods Bridge
#include <CoreAudio/CoreAudio.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#define RB 32768
#define BRIDGE_UID "AirPodsBridge_UID"
static float rb[RB * 2];
static _Atomic uint64_t gW = 0, gR = 0;
static const double gTarget = 2048;
static double gAvg = 2048;
static int gPrimed = 0;
static _Atomic int gUnder = 0, gOver = 0, gSkip = 0, gDup = 0;
static _Atomic float gPeak = 0;
static volatile sig_atomic_t gQuit = 0;
static void onSig(int s) { (void)s; gQuit = 1; }

static OSStatus getProp(AudioObjectID id, AudioObjectPropertySelector s, AudioObjectPropertyScope sc, UInt32 size, void* out) {
  AudioObjectPropertyAddress a = { s, sc, kAudioObjectPropertyElementMain };
  return AudioObjectGetPropertyData(id, &a, 0, NULL, &size, out);
}
static OSStatus setU32(AudioObjectID id, AudioObjectPropertySelector s, UInt32 v) {
  AudioObjectPropertyAddress a = { s, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  return AudioObjectSetPropertyData(id, &a, 0, NULL, sizeof v, &v);
}
static int streamCount(AudioObjectID id, AudioObjectPropertyScope sc) {
  AudioObjectPropertyAddress a = { kAudioDevicePropertyStreams, sc, kAudioObjectPropertyElementMain };
  UInt32 s = 0; AudioObjectGetPropertyDataSize(id, &a, 0, NULL, &s); return (int)(s / sizeof(AudioStreamID));
}
static int cfstr(AudioObjectID id, AudioObjectPropertySelector sel, char* buf, int len) {
  CFStringRef s = NULL; buf[0] = 0;
  if (getProp(id, sel, kAudioObjectPropertyScopeGlobal, sizeof s, &s) || !s) return 0;
  CFStringGetCString(s, buf, len, kCFStringEncodingUTF8); CFRelease(s); return 1;
}
static double rate(AudioObjectID id) { Float64 r = 0; getProp(id, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, sizeof r, &r); return r; }
static int alive(AudioObjectID id) { UInt32 v = 0; return getProp(id, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, sizeof v, &v) == 0 && v; }

// mode 0: bridge by UID; mode 1: first output device whose name contains "AirPods"
static AudioObjectID findDev(int mode, char* nameOut) {
  AudioObjectPropertyAddress a = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
  UInt32 sz = 0; AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz);
  int n = sz / sizeof(AudioObjectID); AudioObjectID ids[128]; if (n > 128) n = 128; sz = n * sizeof(AudioObjectID);
  AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, ids);
  for (int i = 0; i < n; i++) {
    char buf[256];
    if (mode == 0) { cfstr(ids[i], kAudioDevicePropertyDeviceUID, buf, sizeof buf); if (!strcmp(buf, BRIDGE_UID)) return ids[i]; }
    else { cfstr(ids[i], kAudioObjectPropertyName, buf, sizeof buf);
      if (strstr(buf, "AirPods") && streamCount(ids[i], kAudioObjectPropertyScopeOutput) > 0) { if (nameOut) strcpy(nameOut, buf); return ids[i]; } }
  }
  return kAudioObjectUnknown;
}

static OSStatus inProc(AudioObjectID d, const AudioTimeStamp* now, const AudioBufferList* in, const AudioTimeStamp* it,
                       AudioBufferList* out, const AudioTimeStamp* ot, void* ctx) {
  if (!in || in->mNumberBuffers < 1) return 0;
  const AudioBuffer* b = &in->mBuffers[0]; UInt32 ch = b->mNumberChannels; if (!ch || !b->mData) return 0;
  UInt32 n = b->mDataByteSize / (ch * sizeof(float)); const float* src = b->mData;
  uint64_t w = atomic_load_explicit(&gW, memory_order_relaxed), r = atomic_load_explicit(&gR, memory_order_acquire);
  if (w - r + n > RB) { atomic_fetch_add(&gOver, 1); return 0; }
  float pk = atomic_load_explicit(&gPeak, memory_order_relaxed);
  for (UInt32 i = 0; i < n; i++) {
    uint64_t p = (w + i) % RB; float L = src[i * ch], R = ch > 1 ? src[i * ch + 1] : L;
    rb[p * 2] = L; rb[p * 2 + 1] = R; if (fabsf(L) > pk) pk = fabsf(L);
  }
  atomic_store_explicit(&gPeak, pk, memory_order_relaxed);
  atomic_store_explicit(&gW, w + n, memory_order_release);
  return 0;
}
static OSStatus outProc(AudioObjectID d, const AudioTimeStamp* now, const AudioBufferList* in, const AudioTimeStamp* it,
                        AudioBufferList* o, const AudioTimeStamp* ot, void* ctx) {
  if (!o || !o->mNumberBuffers) return 0;
  for (UInt32 b = 0; b < o->mNumberBuffers; b++) if (o->mBuffers[b].mData) memset(o->mBuffers[b].mData, 0, o->mBuffers[b].mDataByteSize);
  UInt32 ch0 = o->mBuffers[0].mNumberChannels ? o->mBuffers[0].mNumberChannels : 1;
  UInt32 n = o->mBuffers[0].mDataByteSize / (ch0 * sizeof(float));
  uint64_t r = atomic_load_explicit(&gR, memory_order_relaxed), w = atomic_load_explicit(&gW, memory_order_acquire);
  uint64_t fill = w - r;
  if (!gPrimed) { if (fill >= gTarget) { gPrimed = 1; gAvg = fill; } else return 0; }
  if (fill < n + 1) { gPrimed = 0; atomic_fetch_add(&gUnder, 1); return 0; }
  if (fill > gTarget + 6000) { r = w - (uint64_t)gTarget; fill = (uint64_t)gTarget; gAvg = gTarget; }
  gAvg = gAvg * 0.995 + (double)fill * 0.005;
  int skip = 0, dup = 0;
  if (gAvg > gTarget + 192) { skip = 1; atomic_fetch_add(&gSkip, 1); }
  else if (gAvg < gTarget - 192) { dup = 1; atomic_fetch_add(&gDup, 1); }
  UInt32 half = n / 2;
  for (UInt32 i = 0; i < n; i++) {
    uint64_t si = r + i; if (i >= half) { if (skip) si += 1; else if (dup) si -= 1; }
    uint64_t p = si % RB; float L = rb[p * 2], R = rb[p * 2 + 1];
    if (o->mNumberBuffers == 1) { float* f = o->mBuffers[0].mData; f[i * ch0] = L; if (ch0 > 1) f[i * ch0 + 1] = R; }
    else { ((float*)o->mBuffers[0].mData)[i] = L; ((float*)o->mBuffers[1].mData)[i] = R; }
  }
  atomic_store_explicit(&gR, r + n + skip - dup, memory_order_release);
  return 0;
}

static double gPhase = 0;
static OSStatus toneProc(AudioObjectID d, const AudioTimeStamp* now, const AudioBufferList* in, const AudioTimeStamp* it,
                         AudioBufferList* o, const AudioTimeStamp* ot, void* ctx) {
  if (!o || !o->mNumberBuffers) return 0;
  AudioBuffer* b = &o->mBuffers[0]; UInt32 ch = b->mNumberChannels ? b->mNumberChannels : 1;
  UInt32 n = b->mDataByteSize / (ch * sizeof(float)); float* f = b->mData;
  for (UInt32 i = 0; i < n; i++) { float v = 0.08f * (float)sin(gPhase); gPhase += 2 * M_PI * 440.0 / 48000.0;
    for (UInt32 c = 0; c < ch; c++) f[i * ch + c] = v; }
  return 0;
}

int main(int argc, char** argv) {
  signal(SIGINT, onSig); signal(SIGTERM, onSig); setvbuf(stdout, NULL, _IOLBF, 0);
  int tone = argc > 1 && !strcmp(argv[1], "tone");
  int secs = argc > 1 + tone ? atoi(argv[1 + tone]) : 0;
  time_t t0 = time(NULL);
  if (tone) {
    AudioObjectID br = findDev(0, NULL); if (!br) { printf("bridge not found\n"); return 1; }
    AudioDeviceIOProcID id; AudioDeviceCreateIOProcID(br, toneProc, NULL, &id); AudioDeviceStart(br, id);
    printf("tone -> AirPods Bridge\n");
    while (!gQuit && (!secs || time(NULL) - t0 < secs)) usleep(100000);
    AudioDeviceStop(br, id); AudioDeviceDestroyIOProcID(br, id); return 0;
  }
  AudioObjectID br = 0, ap = 0; AudioDeviceIOProcID brID = NULL, apID = NULL; char apName[256] = "";
  time_t lastSig = 0;
  #define LOG(...) do { time_t _t = time(NULL); char _b[32]; strftime(_b, sizeof _b, "%F %T", localtime(&_t)); printf("%s ", _b); printf(__VA_ARGS__); } while (0)
  while (!gQuit && (!secs || time(NULL) - t0 < secs)) {
    if (br && !alive(br)) { AudioDeviceStop(br, brID); AudioDeviceDestroyIOProcID(br, brID); br = 0; LOG("bridge lost\n"); }
    if (!br) { br = findDev(0, NULL);
      if (br) { setU32(br, kAudioDevicePropertyBufferFrameSize, 256);
        AudioDeviceCreateIOProcID(br, inProc, NULL, &brID); AudioDeviceStart(br, brID); LOG("bridge open\n"); } }
    float pk = atomic_exchange(&gPeak, 0.0f);
    time_t now = time(NULL); if (pk > 1e-5f) lastSig = now;
    int wanted = lastSig && now - lastSig < 60;
    if (ap && (!alive(ap) || rate(ap) != 48000.0 || !wanted)) {
      AudioDeviceStop(ap, apID); AudioDeviceDestroyIOProcID(ap, apID); ap = 0;
      LOG("AirPods released (%s) under %d skip %d dup %d\n", wanted ? "gone or call mode" : "60 s silence",
          atomic_load(&gUnder), atomic_load(&gSkip), atomic_load(&gDup)); }
    if (!ap && wanted) { AudioObjectID c = findDev(1, apName);
      if (c && rate(c) == 48000.0) { ap = c; setU32(ap, kAudioDevicePropertyBufferFrameSize, 512);
        gPrimed = 0; atomic_store(&gR, atomic_load(&gW));
        AudioDeviceCreateIOProcID(ap, outProc, NULL, &apID); AudioDeviceStart(ap, apID); LOG("forwarding to %s\n", apName); } }
    usleep(50000);
  }
  if (ap) { AudioDeviceStop(ap, apID); AudioDeviceDestroyIOProcID(ap, apID); }
  if (br) { AudioDeviceStop(br, brID); AudioDeviceDestroyIOProcID(br, brID); }
  printf("stopped (under %d skip %d dup %d)\n", atomic_load(&gUnder), atomic_load(&gSkip), atomic_load(&gDup)); return 0;
}
