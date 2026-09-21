import ApplicationServices
// macOS native acceptance driver. Targets only the explicitly supplied test PID.
import Cocoa

let args = CommandLine.arguments
let pid = Int32(args[1])!
let app = AXUIElementCreateApplication(pid)
var value: CFTypeRef?
guard AXUIElementCopyAttributeValue(app, kAXWindowsAttribute as CFString, &value) == .success,
  let windows = value as? [AXUIElement], let window = windows.first
else {
  fputs("test window unavailable (or Accessibility permission missing)\n", stderr)
  exit(2)
}
func bounds() -> CGRect {
  let list = CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as! [[String: Any]]
  let w = list.first {
    ($0[kCGWindowOwnerPID as String] as? Int32) == pid
      && ($0[kCGWindowLayer as String] as? Int) == 0
  }!
  let b = w[kCGWindowBounds as String] as! [String: Double]
  return CGRect(x: b["X"]!, y: b["Y"]!, width: b["Width"]!, height: b["Height"]!)
}
func mouse(_ down: Bool, _ x: Double, _ y: Double) {
  let b = bounds()
  let p = CGPoint(x: b.minX + x, y: b.minY + y)
  CGEvent(
    mouseEventSource: nil, mouseType: down ? .leftMouseDown : .leftMouseUp, mouseCursorPosition: p,
    mouseButton: .left)?.post(tap: .cghidEventTap)
}
func key(_ code: CGKeyCode, _ down: Bool, _ flags: CGEventFlags = []) {
  let e = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down)!
  e.flags = flags
  e.postToPid(pid)
}
switch args[2] {
case "window-id":
  let list = CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as! [[String: Any]]
  let w = list.first {
    ($0[kCGWindowOwnerPID as String] as? Int32) == pid
      && ($0[kCGWindowLayer as String] as? Int) == 0
  }!
  print(w[kCGWindowNumber as String]!)
case "activate":
  NSRunningApplication(processIdentifier: pid)?.activate(options: [])
  AXUIElementSetAttributeValue(app, kAXFrontmostAttribute as CFString, kCFBooleanTrue)
  AXUIElementPerformAction(window, kAXRaiseAction as CFString)
case "click", "down", "up":
  if args[2] != "up" { mouse(true, Double(args[3])!, Double(args[4])!) }
  if args[2] == "click" { Thread.sleep(forTimeInterval: 0.04) }
  if args[2] != "down" { mouse(false, Double(args[3])!, Double(args[4])!) }
case "key":
  key(CGKeyCode(args[3])!, true)
  Thread.sleep(forTimeInterval: 0.04)
  key(CGKeyCode(args[3])!, false)
case "escape":
  key(53, true, [.maskControl, .maskShift])
  key(53, false, [.maskControl, .maskShift])
case "resize":
  var size = CGSize(width: Double(args[3])!, height: Double(args[4])!)
  assert(
    AXUIElementSetAttributeValue(
      window, kAXSizeAttribute as CFString, AXValueCreate(.cgSize, &size)!) == .success)
case "close":
  var button: CFTypeRef?
  assert(
    AXUIElementCopyAttributeValue(window, kAXCloseButtonAttribute as CFString, &button) == .success)
  AXUIElementPerformAction(button as! AXUIElement, kAXPressAction as CFString)
default: fatalError("unknown operation")
}
Thread.sleep(forTimeInterval: 0.08)
