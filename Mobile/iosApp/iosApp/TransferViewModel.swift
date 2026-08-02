import SwiftUI
import Shared

// iOS ViewModel: 桥接 KMP 的 TransferService
@MainActor
class TransferViewModel: ObservableObject {
    @Published var state: TransferStateUI = .idle
    @Published var isTransferring = false

    private let service = TransferService()

    init() {
        // 观察 KMP TransferService 的状态流
        // 注意: 实际项目需通过 Combine/AsyncStream 桥接 StateFlow
        // 这里简化为直接更新
    }

    func sendFile(filePath: String) {
        isTransferring = true
        state = .connecting

        // 调用 KMP TransferService
        // 实际实现需通过 Swift Bridge → C++ 核心库
        service.sendFile(filePath: filePath)

        // 状态更新通过回调 (简化示例)
    }

    func recvFile(roomCode: String, saveDir: String) {
        isTransferring = true
        state = .connecting

        service.recvFile(roomCode: roomCode, saveDir: saveDir)
    }

    func cancel() {
        service.cancel()
        isTransferring = false
        state = .canceled
    }

    func reset() {
        service.reset()
        isTransferring = false
        state = .idle
    }
}

// iOS 侧 UI 状态枚举 (对应 KMP 的 TransferState)
enum TransferStateUI {
    case idle
    case connecting
    case waitingForPeer(String)
    case transferring(Int64, Int64, String)
    case done
    case canceled
    case error(Int, String)
}
