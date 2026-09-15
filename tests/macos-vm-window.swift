// Inspect only a running DeskPort guest window; no event injection or screen recording.
import AppKit
import CoreGraphics
import Foundation

let apps = NSWorkspace.shared.runningApplications.filter {
    $0.bundleIdentifier == "io.github.keithxc.DeskPort"
}
guard apps.count == 1 else {
    fputs("Expected exactly one DeskPort application\n", stderr)
    exit(1)
}
let pid = apps[0].processIdentifier
let all = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
let windows = all.filter { info in
    guard (info[kCGWindowOwnerPID as String] as? Int) == Int(pid),
          (info[kCGWindowLayer as String] as? Int) == 0,
          let bounds = info[kCGWindowBounds as String] as? [String: Any],
          let width = bounds["Width"] as? Double,
          let height = bounds["Height"] as? Double else { return false }
    return width >= 300 && height >= 200
}
guard !windows.isEmpty else {
    fputs("No visible DeskPort content window found\n", stderr)
    exit(1)
}
let report: [String: Any] = ["pid": pid, "visibleWindowCount": windows.count,
                            "bounds": windows.compactMap { $0[kCGWindowBounds as String] }]
let data = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
print(String(decoding: data, as: UTF8.self))
