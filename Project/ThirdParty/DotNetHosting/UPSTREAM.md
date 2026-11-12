# .NET公式Native Hosting headers

`Microsoft.NETCore.App.Host.win-x64` 10.0.13の`runtimes/win-x64/native`に含まれる
以下の公式ヘッダを、開発環境のインストール済みPackから変更せずに収録した。
ヘッダだけを使用し、.NETのバイナリやCoreCLR内部実装は収録しない。
MITライセンスは同梱LICENSE.TXTを参照。upstreamの英語コメントは原文を維持する。

| ファイル | SHA-256 |
|---|---|
| nethost.h | 2049ACB304B6F329F0A68AE7550A5142DF604065AD08663C03FF19D03C0F456C |
| hostfxr.h | F365479A3B54703E288054DF05943A215B8A005D145885E8B0508D1EF596AFC7 |
| coreclr_delegates.h | F1BE3DD52842835B1F824E28A070B9A9626ECD73B1572A1EF772AC07088E8775 |

公式資料:

- [Native Hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)
- [nethost.h](https://github.com/dotnet/runtime/blob/v10.0.0/src/native/corehost/nethost/nethost.h)
- [hostfxr.h](https://github.com/dotnet/runtime/blob/v10.0.0/src/native/corehost/hostfxr.h)
- [coreclr_delegates.h](https://github.com/dotnet/runtime/blob/v10.0.0/src/native/corehost/coreclr_delegates.h)
- [Hosting status codes](https://github.com/dotnet/runtime/blob/v10.0.0/src/native/corehost/error_codes.h)
- [MIT license](https://github.com/dotnet/runtime/blob/v10.0.0/LICENSE.TXT)

Runtime用nethost.dllはユーザーが選択した同世代win-x64 PackからGame Build時に配置する。
ローカルインストールの絶対パスやDLLはGitへ保存しない。最新版の自動取得や暗黙のPack切替は行わない。
