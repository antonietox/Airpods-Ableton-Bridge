// AirPods Bridge menu bar UI: runs the forwarder on a background thread and shows its state.
#import <Cocoa/Cocoa.h>
#include <pthread.h>

extern int fwd_main(int argc, char** argv);
extern int apb_setup(int logToFile);
extern volatile int gUIState;
extern char gUIDevice[256];

static int gArgc; static char** gArgv;
static void* fwdThread(void* a) { fwd_main(gArgc, gArgv); exit(0); return NULL; }

@interface APBMenu : NSObject <NSMenuDelegate>
@end
@implementation APBMenu {
  NSStatusItem* _item; NSMenuItem* _status; int _last;
}
- (void)start {
  _item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
  NSMenu* m = [NSMenu new]; m.delegate = self;
  NSMenuItem* title = [[NSMenuItem alloc] initWithTitle:@"AirPods Bridge" action:nil keyEquivalent:@""]; title.enabled = NO;
  _status = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""]; _status.enabled = NO;
  NSMenuItem* log = [[NSMenuItem alloc] initWithTitle:@"Open Log" action:@selector(openLog) keyEquivalent:@""]; log.target = self;
  [m addItem:title]; [m addItem:_status]; [m addItem:[NSMenuItem separatorItem]]; [m addItem:log];
  _item.menu = m; _last = -1; [self refresh];
  [NSTimer scheduledTimerWithTimeInterval:0.5 target:self selector:@selector(refresh) userInfo:nil repeats:YES];
}
- (void)menuWillOpen:(NSMenu*)menu { [self refresh]; }
- (void)refresh {
  int s = gUIState;
  char dbuf[256]; strncpy(dbuf, gUIDevice, sizeof dbuf - 1); dbuf[sizeof dbuf - 1] = 0;
  NSString* dev = [NSString stringWithUTF8String:dbuf] ?: @"";
  NSString* sym; NSString* txt;
  if (s == 2) { sym = @"headphones.circle.fill"; txt = [NSString stringWithFormat:@"Forwarding to %@", dev]; }
  else if (s == 1) { sym = @"headphones"; txt = @"DAW connected · waiting for audio"; }
  else { sym = @"headphones"; txt = dev.length ? [NSString stringWithFormat:@"Idle · %@ connected", dev] : @"Idle · no Bluetooth headphones"; }
  _status.title = txt;
  if (s != _last) {
    NSImage* img = [NSImage imageWithSystemSymbolName:sym accessibilityDescription:@"AirPods Bridge"]; img.template = YES;
    _item.button.image = img; _item.button.appearsDisabled = (s == 0); _last = s;
  }
}
- (void)openLog {
  NSString* p = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/AirPodsBridge.log"];
  [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:p]];
}
@end

int main(int argc, char** argv) {
  if (argc > 1) return fwd_main(argc, argv);          // CLI modes: tone / permit / timed run
  if (!apb_setup(1)) return 0;                        // already running elsewhere
  @autoreleasepool {
    gArgc = argc; gArgv = argv;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    APBMenu* menu = [APBMenu new]; [menu start];
    pthread_t t; pthread_create(&t, NULL, fwdThread, NULL);
    [NSApp run];
  }
  return 0;
}
