// leduong: local-only loader. Automatic remote updates are disabled.
import Combine
import Foundation

final class CheckUpdateManager {
    static let shared = CheckUpdateManager()
    struct LatestVersion: Codable {
        let tagName: String
        let tipaAssetURLString: String
        let versionString: String
        let buildVersionString: String
    }
    let latestVersionSubject = CurrentValueSubject<LatestVersion?, Never>(nil)
    var shouldPopCheckUpdateAlert: Bool { false }
    var isNewVersionAvailable: Bool { false }
    private init() {}
    func delayCheckUpdateAlert() {}
    func checkUpdateIfNeeded(completion: ((LatestVersion?, Error?) -> Void)? = nil) {
        completion?(nil, nil)
    }
    func executeUpgrade() {}
}
