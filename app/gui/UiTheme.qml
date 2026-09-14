import QtQuick 2.9
QtObject {
    property int mode: 0
    property int accentMode: 0
    property SystemPalette systemPalette: SystemPalette { colorGroup: SystemPalette.Active }
    property bool systemDark: systemPalette.window.hslLightness < 0.5
    readonly property bool dark: mode === 2 || (mode === 0 && systemDark)
    readonly property color canvas: dark ? "#141719" : "#f7f8fa"
    readonly property color surface: dark ? "#1e2326" : "#ffffff"
    readonly property color raised: dark ? "#272e32" : "#eef1f6"
    readonly property color line: dark ? "#343d42" : "#dce2ea"
    readonly property color text: dark ? "#f1f4f3" : "#222b3a"
    readonly property color muted: dark ? "#a4b1b5" : "#586579"
    function luminance(c) {
        function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b)
    }
    property color systemAccent: systemPalette.highlight
    readonly property color baseAccent: accentMode === 1 ? "#3269d7" : accentMode === 2 ? "#23754f" : accentMode === 3 ? "#8055bf" : accentMode === 4 ? "#ae560f" : systemAccent
    readonly property color accent: {
        // Keep accent labels legible even when an OS accent is very pale or dark.
        var c = baseAccent
        var background = luminance(canvas)
        for (var i = 0; i < 18; ++i) {
            var l = luminance(c)
            if ((Math.max(l, background) + 0.05) / (Math.min(l, background) + 0.05) >= 4.5) break
            c = Qt.hsla(Math.max(0, c.hslHue), c.hslSaturation, Math.max(0, Math.min(1, c.hslLightness + (dark ? 0.035 : -0.035))), 1)
        }
        return c
    }
    readonly property color accentText: luminance(accent) > 0.179 ? "#000000" : "#ffffff"
    readonly property color warning: dark ? "#f0c987" : "#855300"
    readonly property int small: 12
    readonly property int body: 14
    readonly property int title: 20
    readonly property int heading: 26
    readonly property int gap: 12
    readonly property int padding: 20
    readonly property int radius: 12
}
