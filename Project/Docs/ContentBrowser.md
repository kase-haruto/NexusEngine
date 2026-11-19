# Content Browser

## 操作

- SourcesにContent / Source / Levels / Configと、実在するEngine Assets・Project Plugins内のContent／Assetsを表示します。Engine／Plugin／Configは閲覧専用です。表示設定でEngine／Pluginを切り替えられます。
- 戻る／進む／親フォルダー、Breadcrumb、パス入力、Grid／List／Details、Sort、複数Type Filter、お気に入りを使用できます。Ctrl／Shift／Ctrl+Aで項目選択、SourcesのCtrlクリックで複数フォルダーを検索対象にします。
- Toolsの「Content Browserを追加」で最大4ウィンドウを開けます。移動先・検索・Filter・選択はウィンドウごとに独立し、Collection・文書・CPU indexを共有します。
- コレクションを作成・改名・解除し、右クリックまたはドロップで項目を追加できます。LocalはUser.settings、SharedはProjectのConfig/ContentCollections.jsonに保存します。Collectionの削除はファイルを消しません。他EditorのShared変更を古い内容で上書きしません。Shared内容の外部変更はRefreshで再読み込みします。
- 最近開いた項目は実際のOpenから記録します。最近変更された項目は明示選択時にProject indexを作り、更新日の新しい順に表示します。Saved Searchには検索式・再帰設定・対象フォルダー・Type Filterを保存します。同じ名前で保存すると更新できます。

## 検索

名前の語、`type:Texture`／`type:Cpp`／`type:Material`、`ext:.png`、`path:Characters`をANDで組み合わせます。引用符で空白を含む語を指定できます。

追加条件:

- `size:>4MB`、`size:<=512KiB`。B／KB／MB／GBとKiB／MiB／GiBに対応します。
- `modified:today`、`modified:yesterday`、`modified:7d`、`modified:>=2026-11-01`。日付はローカル時刻です。
- `triangles:>10000`。対応するOBJ／glTF／GLBのCPU metadataから求めます。GPUへのアップロードは行いません。
- 「サブフォルダも検索」で非同期の再帰indexを使用します。上限100,000項目。シンボリックリンク／root外の再解析先へ降りません。Build／Generated／obj／bin／.git／.vs／.nexus-trashを除外します。

indexは初回要求と変更通知後にだけ更新します。安定したframeでは不変snapshotを再利用し、全件copy・folder列挙・ファイルの再検証を繰り返しません。連続変更中はindexの再構築を750ms待ってまとめます。

## インポート・再インポート

Toolbarのインポートから複数ファイルを選び、「相対参照の依存ファイルもコピー」「既存ファイルを置換」を指定します。「インポート内容を確認」で変更予定を作り、追加／置換対象を確認してから適用します。準備・依存調査・読込はworkerで実行します。取消では公開しません。

対応する既存ファイル形式をそのまま取り込みます。相対URIを持つglTF／OBJ／MTL等は、解決できる同じ外部フォルダー内の依存ファイルを同じ相対配置でコピーします。未解決の依存やroot外の依存を黙って省略しません。必要なら依存コピーを外し、適切な配置を利用者が指定してください。新しい独自形式への変換や、Rendererが非対応の形式への対応を装う機能ではありません。

初期選択256ファイル、依存を含め1,024ファイル、読込＋置換backup合計128MiBを上限とします。大きい取り込みは分割してください。元ファイルが準備中に変更された場合は再試行を求めます。既存宛先は明示した場合だけ置換し、GUIDを維持します。未保存の編集中文書への置換は拒否します。

Import元の絶対パス・hash・依存設定は利用者別のUser.settingsへ保存し、共有Assetに個人の絶対パスを埋め込みません。右クリックの再インポートで再利用します。Asset Inspectorにも元パスを表示します。改名後もGUIDでOriginを見つけ、現在のファイル名へ再インポートします。別PCでは最初にImportからOriginを選び直してください。

Import／Reimportは共通Undo／Redoに対応します。新規コピーのUndoは所有archiveへ退避し、Redoでも同じGUIDを戻します。外部編集された内容をUndo／Redoで上書きしません。

## 保存とAsset情報

「すべて保存」は実際のLevel、Animation Studio、Curve、Rig／Layer、Render Graph、Input Mapのdirty文書を保存します。未保存の新規Levelは既存のSave Asを要求します。失敗した文書をclean扱いにせず、失敗理由を集計します。

項目を選ぶとDetailsをAsset Inspectorとして表示し、GUID、ファイルサイズ、型、Textureの解像度／元pixel bits／mip、Modelの頂点／mesh／材質／三角形数、Sourceの言語／namespace／classを表示します。Entityを再選択するか「Entityのプロパティを表示」で戻れます。drag中は従来のEntity Detailsを表示してdrop領域を維持します。

右クリックの依存関係／参照元は、登録されたProject rootを明示要求時に非同期で調査します。既知のJSON文字列、asset://ID、glTF URI、OBJ／MTL、Source includeと、登録型のsourceFilesに対応します。Level等の登録Script／Component IDもSourceへの参照として扱います。未知形式、コードで動的生成するパス、実行時に合成する型名まで静的に証明するものではありません。読み取れない情報や未解決参照は診断として表示します。

## ファイル操作とSource

Assetファイル／フォルダーの移動・改名はworkerで影響を調査し、移動と参照更新の一覧を確認して適用します。glTF／GLBの外部URI、OBJ／MTLの参照、既知Asset文字列、Source includeとNative登録のsourceFilesを更新します。GUIDと旧path aliasを維持し、一つのUndo／Redoへ記録します。宛先衝突、自己フォルダー内への移動、root外、dirty文書、準備後の外部変更を拒否します。移動4,096ファイル、文字列書換128MiB、調査index100,000項目が上限です。

ファイルの複製には新しいGUIDを発行します。Assetフォルダーの複製はImportと同じ非同期確認を使います。削除はProject rootの`.nexus-trash/<operation ID>/`への退避です。物理削除やarchiveの自動消去は行わず、Undoで復元します。空でない元フォルダーを強制削除しません。

Sourceの作成画面ではModule、Behaviour／Class／Header／Source、Namespace、基底クラス、基底ヘッダー、Module内のフォルダーを指定できます。C#はBehaviour／Classに対応します。Behaviourは既存の明示登録とBuild／Reload手順へ通します。通常クラスはComponentとして自動登録しません。

Sourceの改名・移動は同一Game Module内で行い、対応するC++ header／cpp、include、登録sourceFilesを合わせて更新します。ファイル名を変えてもクラス名と保存済みType IDは維持します。複製は作成画面で別名を指定し、自己型の識別子と新しい登録IDを作ります。文字列literal・commentを識別子の置換対象にしません。変更後は既存のBuild／Reloadを実行してください。

Moduleのroot／Main.cpp／GameComponents.h／生成registry自体の移動、登録済みNative Behaviourの削除、別ModuleへのABI移行は自動化しません。これらはProject／登録情報の明示変更が必要です。任意C++の意味を保つ汎用refactorを装って危険な移動を有効化しません。

filesystem全体のACID transactionは保証しません。一般の外部編集は適用前のsignature／内容比較で拒否し、I/O失敗時は適用済み分の復元を試みます。hardware障害や競合で復元できない場合はエラーと所有archiveを確認してください。

## Drag & Drop

単一Asset／Sourceは従来のNEXUS_ASSET_PATH／NEXUS_SOURCE_PATHを維持します。複数項目とfolderはversion付きNEXUS_CONTENT_ITEMSへ値コピーし、256項目／1MiB、絶対path・長さ・JSON構造を検証します。Folderへのdropは移動確認、Collectionへは参照の追加、Project ModelのViewport dropは複数Entityの一括作成へ通します。複数SourceはDetails／Outlinerで型を一意に解決し、一つのScene transactionで追加します。

単一Asset用Propertyは複数payloadを暗黙にprimaryへ切り詰めません。Engine／PluginのModelは最初にProjectへImportしてください。

## 外部変更の自動反映

Project rootでは既定で有効にし、表示設定から停止できます。利用者別に設定を保存します。WindowsのReadDirectoryChangesW＋OVERLAPPED eventを使い、定期的な全ファイル走査や専用の常駐CPU loopは使いません。

8ms以内の重複event確認を省き、250msの変更をまとめて、影響する閲覧cacheだけを無効化します。thumbnailは表示されたものだけを一つずつ非同期で準備します。変更通知だけでシーン全体のGPU資源を再作成しません。反映後のモデル本体は明示Refresh／Reimportで更新できます。

1秒5,000通知、1,024変更folder、1秒8回のoverflowを超えると監視を停止し、手動Refreshへ戻します。通知bufferのoverflowだけで全treeを走査しません。自動更新したfolderの実際の読込が16msを3回続けて超えた場合も、自動監視を停止します。ネットワークrootやOS通知が利用できないrootも手動更新です。画面を開いたままのidleでfolderを再走査しないことを回帰テストします。

## アイコン・拡張

対応表・実際の参照パス・状態・サイズ・新規／再利用／専用未配置の一覧は[ContentBrowserIconList.md](ContentBrowserIconList.md)。PNGだけの差し替え、実行中の再読み込み、Theme設定とGPU資源の寿命も記載しています。

新形式はContentItemRegistryにID・labelKey・Icon ID・拡張子・Preview能力を登録します。Openや型固有操作は既存serviceへのcallbackを登録してください。非同期indexには表示情報だけのMetadataOnly snapshotを渡します。新rootはWorkspace共有ContentBrowserServicesへ登録し、Panelより長いCatalog所有者を用意します。

主な責務: ContentBrowserPanel＝表示と要求、ContentLibrary＝Collection／Recent／Saved Search、AssetDocumentRegistry＝実文書の保存、ContentIndex／ContentMetadata／ContentDependencyGraph＝非同期読取、ContentImportService／ContentMutationService＝確認と履歴、DirectoryChangeWatcher＝OS通知、EditorIcons／ImGuiRenderer＝画像解決とGPU寿命です。
