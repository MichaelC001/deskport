import Foundation
import Vision

let request = VNDetectBarcodesRequest()
request.symbologies = [.qr]
let handler = VNImageRequestHandler(url: URL(fileURLWithPath: CommandLine.arguments[1]))
try handler.perform([request])
let values = (request.results ?? []).compactMap { $0.payloadStringValue }
guard values.count == 1 else { fatalError("Expected exactly one decoded QR") }
print(values[0])
