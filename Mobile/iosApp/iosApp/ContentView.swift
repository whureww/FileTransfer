import SwiftUI
import Shared

struct ContentView: View {
    @StateObject private var viewModel = TransferViewModel()
    @State private var mode: TransferMode = .send
    @State private var filePath = ""
    @State private var roomCode = ""
    @State private var relayHost = "your-relay-server.com"
    @State private var relayPort = "9091"

    var body: some View {
        NavigationView {
            VStack(spacing: 20) {
                Text("FileTransfer v0.0.8")
                    .font(.title2)
                    .fontWeight(.bold)

                // 模式切换
                Picker("模式", selection: $mode) {
                    Text("发送").tag(TransferMode.send)
                    Text("接收").tag(TransferMode.recv)
                }
                .pickerStyle(.segmented)

                // 中继服务器配置
                VStack(spacing: 8) {
                    TextField("中继服务器地址", text: $relayHost)
                        .textFieldStyle(.roundedBorder)
                    TextField("端口", text: $relayPort)
                        .textFieldStyle(.roundedBorder)
                        .keyboardType(.numberPad)
                }

                // 根据模式显示不同面板
                switch mode {
                case .send:
                    SendPanel(viewModel: viewModel, filePath: $filePath)
                case .recv:
                    RecvPanel(viewModel: viewModel, roomCode: $roomCode)
                }

                // 状态显示
                StatusView(viewModel: viewModel)

                Spacer()
            }
            .padding()
            .navigationTitle("FileTransfer")
        }
    }
}

struct SendPanel: View {
    @ObservedObject var viewModel: TransferViewModel
    @Binding var filePath: String

    var body: some View {
        VStack(spacing: 12) {
            TextField("文件路径", text: $filePath)
                .textFieldStyle(.roundedBorder)

            Button(action: {
                viewModel.sendFile(filePath: filePath)
            }) {
                Text("开始发送")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(filePath.isEmpty || viewModel.isTransferring)
        }
    }
}

struct RecvPanel: View {
    @ObservedObject var viewModel: TransferViewModel
    @Binding var roomCode: String

    var body: some View {
        VStack(spacing: 12) {
            TextField("输入 6 位房间码", text: $roomCode)
                .textFieldStyle(.roundedBorder)
                .textInputAutocapitalization(.characters)
                .multilineTextAlignment(.center)
                .font(.system(.title3, design: .monospaced))

            Button(action: {
                let dir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].path
                viewModel.recvFile(roomCode: roomCode, saveDir: dir)
            }) {
                Text("开始接收")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(roomCode.count != 6 || viewModel.isTransferring)
        }
    }
}

struct StatusView: View {
    @ObservedObject var viewModel: TransferViewModel

    var body: some View {
        switch viewModel.state {
        case .idle:
            EmptyView()
        case .connecting:
            VStack(spacing: 8) {
                ProgressView()
                Text("正在连接中继服务器...")
            }
        case .waitingForPeer(let code):
            VStack(spacing: 8) {
                Text("房间码")
                    .font(.caption)
                Text(code)
                    .font(.system(size: 48, weight: .bold, design: .monospaced))
                Text("将此房间码告知接收方")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        case .transferring(let done, let total, let message):
            VStack(spacing: 8) {
                ProgressView(value: Double(done), total: Double(total))
                Text("\(formatBytes(done)) / \(formatBytes(total))")
                if !message.isEmpty {
                    Text(message).font(.caption)
                }
                Button("取消", role: .destructive) {
                    viewModel.cancel()
                }
            }
        case .done:
            VStack(spacing: 8) {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundColor(.green)
                    .font(.largeTitle)
                Text("传输完成")
                Button("返回") { viewModel.reset() }
            }
        case .canceled:
            VStack(spacing: 8) {
                Image(systemName: "xmark.circle.fill")
                    .foregroundColor(.orange)
                    .font(.largeTitle)
                Text("已取消")
                Button("返回") { viewModel.reset() }
            }
        case .error(let code, let message):
            VStack(spacing: 8) {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.red)
                    .font(.largeTitle)
                Text("错误: \(message) (code=\(code))")
                Button("返回") { viewModel.reset() }
            }
        }
    }
}

func formatBytes(_ bytes: Int64) -> String {
    if bytes < 1024 { return "\(bytes) B" }
    let kb = Double(bytes) / 1024
    if kb < 1024 { return String(format: "%.1f KB", kb) }
    let mb = kb / 1024
    if mb < 1024 { return String(format: "%.1f MB", mb) }
    return String(format: "%.1f GB", mb / 1024)
}
