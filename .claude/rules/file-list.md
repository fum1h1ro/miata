---
paths:
  - "src/models/FileListModel.*"
  - "src/models/FileEntryModel.*"
  - "src/Utf8.*"
---

# ファイル一覧（右側の札・ドラッグ&ドロップ・再読み込み・ディレクトリ監視・空の一覧・不正な名前）

> 機能別の設計メモ。上の `paths:` のファイルを触ると自動で読み込まれる。`Application.cc` など複数の機能が同居するファイルでは載らないので、そのときは CLAUDE.md の「設計メモの索引」から、該当するファイルを自分で読むこと。
> テスト・実測・実機未確認の記録は `.claude/notes/file-list.md`（自動では読み込まれない。テストを書く・直す・検証するときに読む）。

## 一覧の右側の札（`<DIR>` / `<LNK>` / `<ALIAS>` / `<CLOUD>`）

行の右側の、サイズの欄に、フォルダとリンクは、サイズの代わりに札を出す。ダウンロード前のファイル（`<CLOUD>`）は、サイズを残して、その前に札を足す（後述）。ユーザー向けの仕様は README の「一覧の右側の表示」。ここには、設計の決定と罠を書く。以前は `<DIR>` だけで、シンボリックリンクと Finder のエイリアスを見分けられなかった（フォルダへのシンボリックリンクは `<DIR>` と出て、フォルダと同じに見えた。ファイルへのリンクはリンク先のサイズ、壊れたリンクは `0B`、エイリアスは通常のファイルのサイズ）。

- **流れ**：`FileListView::Draw` → `SizeColumn(entry)`（`FileListView.mm` の無名名前空間。札 `tag`・サイズ `size`・札の色の種類 `tag_color` を持つ `SizeCell` を返す。`Text()` が欄の文字列）→ `FileEntryModel::IsSymlink()` / `IsAlias()` / `IsDirectory()` / `IsDataless()`。**判定の順は、シンボリックリンク（`<LNK>`）→ エイリアス（`<ALIAS>`）→ フォルダ（`<DIR>`）→ ダウンロード前のファイル（`<CLOUD>` + サイズ。後述）→ サイズ**：`IsDirectory()` はリンク先を見る（`directory_entry::is_directory`）ので、リンクを先に見ないと、フォルダへのリンクが `<DIR>` になる。`IsSymlink()` は readdir の `d_type` で分かる（システムコールなし）。`IsAlias()` は `pl_is_alias_file`（下記）を、毎回呼ぶ（1 回 1〜2µs。結果は持たない。描くのは見えている行だけで、`Size()` も行ごとに調べている）
- **決めたこと**：札は、リンク先の種類に関わらず付く（フォルダへのシンボリックリンクも `<LNK>`。リンク先のサイズは出ない）。シンボリックリンクとエイリアスは、別の札（依頼は「symlink や alias を見分けられない」。ファイルシステムの上でも別物で、エイリアスは通常のファイル）。**行の文字（名前・更新日時）の色は変えない**（`IsDirectory()` のまま。フォルダへのシンボリックリンクはフォルダの色、エイリアスはファイルの色）。色が付くのは札の部分だけ（下記）。並べ替え・Lua の `is_dir`・Enter・コピー・移動は、変えていない（表示だけ）
- **札の色（`symlink` / `alias`）**：依頼は「`<xxx>` で囲まれた部分だけ色を変えたい。`<DIR>` はそのまま、LNK と ALIAS は別の色」。**設定の色にした**（`Config::Color` の `Symlink` / `Alias` = `Miata.config.color.symlink` / `alias`。既定値は `resources/test.lua`。ほかの色と同じ流儀）。**2 つに分けた**のは、「別の色」が、「ふつうの文字と別」とも「2 つが互いに別」とも読めるため（同じ色にしたければ、同じ値を書けばよい）。既定は、黄 `#ffd24d`（`<LNK>`）とピンク `#ff8ad8`（`<ALIAS>`）：白（ファイル）・ミント `#00ffaa`（フォルダ）・ライム（ヘッダー）と見分けがつき、マークした行の暗い青の背景の上でも読める（PNG で確認。Osaka 20pt でも）。`SizeColumn` が `tag_color`（`std::optional<Config::Color::Type>`。`<LNK>` が `Symlink`、`<ALIAS>` が `Alias`、`<CLOUD>` が `Cloud`、`<DIR>` とサイズは nullopt）を返し、`Draw` は、**`tag_color` があるときだけ**、右欄の文字列全体の `NSMutableAttributedString`（行の色の属性）の、先頭の `tag` の長さの範囲（札の `<` から `>` まで）に `NSForegroundColorAttributeName` を足して、`drawInRect:` で描く。それ以外は、これまでの `drawInRect:withAttributes:`。色で大きさは変わらないので、右寄せの位置は、ふつうの文字列で測ったまま。枠の幅は、測った幅のまま（余白を足さない）：属性つきの文字列でも、折り返さない（実測: 16 のフォント × 6〜48pt × 6 つの文字列の 7650 通りで、測った幅の枠に、1 行で収まった。余白を 0 にする変異が、どのテストにも検出されなかった=要らないので、余白ごと消した）。**既定値は `resources/test.lua` に要る**（無いと、`Color4f` の既定の不透明な黒で描かれて、暗い背景に沈む。テスト `defaults`）。**名前を足したアプリと `test.lua` は、同時に更新する**（古いアプリが新しい `test.lua` を読むと、未知の色名のエラーで、後ろの設定が実行されない）
- **エイリアスの判定 `pl_is_alias_file`**（`platform.h` / `osx.mm`）：`getattrlist(path, ATTR_CMN_FNDRINFO, FSOPT_NOFOLLOW)` で、Finder 情報（32 バイト）の `finderFlags`（オフセット 8 のビッグエンディアン 16 ビット）の `kIsAlias`（0x8000）を見る。**`NSURLIsAliasFileKey` は使わない**：シンボリックリンクにも真を返す（実測。ファイルへの・フォルダへの・壊れたもの、どれも 1）ので、2 つの札に分けられない。`getattrlist` のほうが、3 倍ほど速い（実測: 1.7µs と 4.9µs）。**パスは `c_str()` のバイト列のまま渡す**（NSString にすると、UTF-8 として不正な名前で nil。「名前が UTF-8 として不正なファイル」と同じ）。`FSOPT_NOFOLLOW`：シンボリックリンクは、リンクそのもの（印は無い）を見るので、エイリアスへのシンボリックリンクは `<LNK>`。調べられないとき（存在しない・権限が無い・Finder 情報を持たないボリューム）は false。**印だけを見る**ので、印が立っていれば、ふつうのファイルでも `<ALIAS>`。リンク先は見ない（元が消えたエイリアスも `<ALIAS>`）
- **壊れたシンボリックリンクは、更新日時が空**：`FileEntryModel` のコンストラクタが、リンク先があるときだけ更新日時を読むため（以前からの挙動。直していない。札が付いたので、壊れたリンクと分かる）
- **ダウンロード前のファイル（`<CLOUD>`。2026-10-09）**：依頼は「CloudStorage で、ファイルのエントリーはあるけど実体はまだダウンロードされてない状態って認識できるのかな？できるなら表示したい」。**認識できる**（実測。macOS 27.0、Dropbox-Personal。深さ 2 までのファイル 299 件のうち、80 件が dataless）。**判定 `pl_is_dataless_file`**（`platform.h` / `osx.mm`）：`lstat` の `st_flags & SF_DATALESS`（`0x40000000`。`sys/stat.h` に「Synthetic flags: read-only」とある、書き込めない印。`ls -lO` の `dataless`）。dataless のファイルは、`st_flags` が `compressed,dataless`、**`st_size` は本来のサイズのまま**（`<LNK>` と違って、サイズが分かる）、`st_blocks` が 0（実体のあるファイルは 0 でない）。同じ答えが、`getattrlist(ATTR_CMN_FLAGS)` と、NSURL の `NSURLUbiquitousItemDownloadingStatusKey`（`NotDownloaded` / `Current`。Dropbox の File Provider でも効く）でも得られる。**NSURL のキーは使わない**：パスを NSString / NSURL にするので、UTF-8 として不正な名前で使えない（`pl_is_alias_file` と同じ理由）。`lstat` と `getattrlist` は同じ速さ（実測: dataless のファイル 2〜3µs、実体ありのファイル 1µs）。**メタデータだけなので、ダウンロードは起きない**（実測: `lstat`・`getattrlist(FLAGS)`・`getattrlist(FNDRINFO)`=エイリアスの判定を当てた前後で、dataless のまま・`st_blocks` は 0 のまま）。パスは `c_str()` のバイト列のまま。`lstat` なので、シンボリックリンクはリンクそのものを見る（ローカルのリンクに、この印は付かない=実測。クラウドストレージの側が、リンクに印を付けることがあるかは未確認。Dropbox はシンボリックリンクを同期しないので、確かめられなかった。表示は `<LNK>` を先に判定するので、どちらでも `<LNK>` が勝つ）。調べられないとき（存在しない・権限が無い）は false。`~/Library/CloudStorage` に限らない（OS の印を見るだけ。iCloud Drive の実体は `~/Library/Mobile Documents/com~apple~CloudDocs` で、直下の evicted なファイルも `compressed,dataless`=実測。ユーザーが実機で、Dropbox・Google Drive・iCloud Drive の 3 つで、札が正しく出ることを確認した（2026-10-09）。OneDrive は未確認）
- **札の出し方（ユーザーの決定: 「札を足す」）**：`<LNK>` などと違って、**サイズを置き換えず、サイズの前に足す**（`<CLOUD> 82.7M`）：サイズが分かっていて、ダウンロードするかを決める材料になる（86MB の PDF もある）。ほかの案（色だけ・サイズを札に置き換える）は、選ばれなかった。`SizeCell` は `{tag, size, tag_color}`（`Text()` が、札とサイズを空白で区切る）。`Draw` が色を付けるのは、`tag` の長さの範囲だけ（サイズは行の色のまま）。右の欄は、札のぶん（`<CLOUD> ` の 8 文字）広がり、名前の欄は、その分、狭くなる（`Draw` が右欄の幅から名前の欄を決めるので、そのまま。Osaka 20pt で、名前と重ならないことを PNG で確認）。**`IsDataless()` は、リンク・エイリアス・フォルダを除いた、ふつうのファイルにだけ呼ぶ**（`SizeColumn` の最後。1 回 1〜3µs）。リンクの札が常に勝つ（`IsSymlink()` を先に見る）。エイリアスのファイル自体が dataless でも、`<ALIAS>` が勝つ
- **フォルダには出さない（ユーザーの決定）**：フォルダにも `SF_DATALESS` が付くことがある（実測: 作って間もない空フォルダ 1 件）が、意味が違う（「中身の一覧をまだ取っていない」）。**一覧（readdir）すると外れる**（実測 1 回: `ls` で一覧した後、同じフォルダの印が消えた。Miata の `Scan` も同じ読み出しなので、入ると外れるはず=未確認）。フォルダは `<DIR>` のまま。`FileEntryModel::IsDataless()` は、フォルダにも真を返しうる（印をそのまま返す）が、一覧の表示は、ファイルにだけ使う
- **色（`cloud`）**：`Config::Color` の `Cloud` = `Miata.config.color.cloud`。既定は水色 `#7ec8ff`（白・ミント・黄・ピンクと見分けがつき、暗い背景でも読める。PNG で確認。Osaka 20pt でも）。**既定値は `resources/test.lua` に要る**（無いと、不透明な黒で描かれて、暗い背景に沈む。テスト `defaults`）。アプリと `test.lua` は、同時に更新する（上記）
- **表示だけ**：並べ替え（サイズは、本来のサイズで並ぶ）・Lua の `is_dir`・Enter・コピー・移動は、変えていない。**見送ったもの（ユーザーが選ばなかった）**：Lua のエントリの項目（`dataless` など）、Quick Look が dataless のファイルをダウンロードしないようにする仕組み、開く・コピーの前の確認。**ダウンロードが起きる操作**：中身を読む操作（開く=`NSWorkspace`、Quick Look、コピーの `copyfile`、ドラッグ先のアプリ）。Quick Look は、カーソルに追従する（前述「プレビュー」）ので、開いたまま dataless のファイルへカーソルが乗ると、ダウンロードが始まるはず（未確認）。コピー・移動の事前の走査 `MeasureCopyBytes` は `st_size` を読むだけなので、ダウンロードは、`copyfile` が読むときに起きる（コピーが遅くなる。進捗パネルには、遅いコピーとして出る）
- **更新**：描くたびに（見えている行だけ）調べる。キャッシュしない（ダウンロードが終わったあとの、次の描き直しで、札が消える。自動リロード（FSEvents）が、フラグの変化を拾えば、自動で更新される=未確認）

## マーク済みファイルのドラッグ&ドロップ（他アプリへの持ち出し）

ファイル一覧は基本キーボード操作で、マウスを受け付けるのはこれだけ（クリックでのカーソル移動等は無い。ダイアログの選択リストの行のクリックは別。後述「選択リスト」）。実装は `views/FileListView.mm` の `_MiataFileListNSView`（`mouseDown:`/`mouseDragged:` + `NSDraggingSource`）と `FileListView::BeginDrag()`/`EndDrag()`。

- **運ぶのは「そのペインのマーク済みファイル」だけ**。押した行がどれかは判定しない（未マークの行から始めても同じ）。マークが0件なら何もしない。順序は画面表示順（`list_` 順）。絞り込みで隠れている行のマークは運ばない（見えているマークだけ）。
- **ダイアログ表示中は無効**。`DialogPanel` は画面中央の小さな `NSView` でマウスを遮らないため、裏の一覧にもマウスが届く。`View` のコンストラクタが `SetDragGuard`（`BrowserView` 経由で左右ペインへ）で `!IsAnyDialogOpened()` を注入している（`FileListView` は `View` を知らない）。
- `_MiataFileListNSView` は `acceptsFirstResponder` を YES にしないこと（キー入力は `MiataRootView` に届き続ける必要がある）。
- **ドロップ後（`EndDrag`）**：宛先（Finder 等）は受理後に非同期で移動を進めるため、0.3 秒待ってから**実ファイルの有無**で判断する。1件でも消えていれば `Reload()` で再スキャン（カーソルは維持され、移されなかったファイルのマークは残る）、そうでなければ、運んだファイルのマークだけ `UnmarkPaths()` で外す（そのペインの全マークではない）。宛先が申告する操作種別（Move/Copy/Generic）は自己申告なので当てにしない。キャンセル/拒否時はマーク維持。宛先の処理が 0.3 秒より遅くても、その後の一覧への反映は「ディレクトリ監視」（後述）が追随する（マークを解除するかどうかの判断だけは、この 0.3 秒の時点で決まる）。
- **罠**：`NSDraggingItem.draggingFrame` にサイズ 0 は指定できず `NSRangeException` で落ちる。画像を省く件（`contents=nil`）でも枠は非ゼロにすること。多数マーク時にアイコンを先頭 16 件に絞っているのはこのため（残りは画像なしで運ぶ）。
- 自アプリ内へのドロップ（ペイン間ドラッグ）は未実装（`NSDraggingDestination` が必要）。

## 一覧の再読み込み（`reload`）

`Miata.command.reload([pane])` → `Application::lua_command_reload` → `FileListView::Reload()` → `FileListModel::Reload()`。1つのペイン（引数省略/nil なら現在のペイン、`"left"`/`"right"` ならそのペイン）だけを再スキャンし、**マークとカーソルをパスで引き継ぐ**。

- **ペインの表し方**：C++ では `views::constants::Pane`（`Constants.h`）、Lua には `"left"`/`"right"` の文字列で公開する。文字列⇔enum の変換は `Application.cc` の `PaneName()`/`ParsePane()` に集約し、`Miata.command.current_pane()`（戻り値）と `reload(pane)`（引数）が同じ表記を使うようにしている（戻り値をそのまま引数に渡せることが前提）。今後ペインを引数に取るコマンドを足すときもこれを使う。ペインを指定して取るアクセサは `View::CurrentPane()`/`GetFileListView(pane)`/`GetList(pane)`。
- 対象が現在のペインでなくても、カーソル・マークの復元は同じ（各ペインが自分の `cursorIndex_` とモデルを持つため）。フォーカスは動かさない。

- **役割分担**：マークはモデル（`FileEntryModel` のフラグ）なので `FileListModel::Reload()` が新旧をパスで突き合わせて引き継ぐ。カーソルはビューの状態で、`cursorIndex_` は「画面表示順の添字」なので、`FileListView` がパスで復元する（並びが変わっても同じファイルに追従。消えていたら再スキャン前の並びで次に残っているもの→無ければその前→無ければ先頭）。
- **罠（寿命）**：旧エントリはモデルの再スキャン時（`ObservePath()` の通知**より前**）に破棄される。ビューが通知を受けてから `list_` を読むと寿命切れの参照になるので、カーソル復元用の記録（`CursorMemo`：表示順のパス列とカーソル位置）は**再スキャン前に**パスとして控え、通知の購読側が `Fetch()` の直後に `RestoreCursor()` する。`FileListModel` は再スキャンで `entries_` を差し替えたら必ず `ObservePath()` へ通知すること（ビューが `list_` を作り直せるように）。
- **`JumpTo` は「ディレクトリを移動する」専用**（カーソルは先頭、マークは消える）。同じディレクトリを再スキャンして最新にしたいだけの処理（mkdir/rename/`trash`/コピー・移動の完了後/ドロップ後）は `JumpTo(Path())` ではなく **`View::ReloadList(model, cursor_to)`** を使う（カーソルとマークが維持され、ゴミ箱や移動に失敗したファイルのマークも残る。再スキャン自体の失敗は無視する）。新しくそういう処理を足すときも `JumpTo(Path())` を使わないこと。
- `Reload` / `ReloadList` の `cursor_to`：旧パスが消えて新しいパスに移る操作（リネーム）では新しいパスを渡す。渡さないと、旧名が消えるのでカーソルは「次のファイル」に寄ってしまう。`rename_to`（`Application::RenamePath`）は、`View::ReloadDirectory(dir, {旧パス, 新パス})` を使う：そのフォルダを表示しているペイン（左右が同じなら両方）を引いて、**カーソルが旧パスにあるペインだけ**、新しいパスへ寄せる（別のファイルにカーソルがあるペインに `cursor_to` を渡すと、カーソルを奪ってしまう）。別のフォルダへ移したときは、旧パスのフォルダと移した先の両方を再スキャンする（寄せるのは、同じフォルダの中での変更だけ）。Copy の移動元は中身が変わらないので、`Reload` ではなく、操作したファイルのマークだけを外す（`UnmarkPaths`）。
- **モデルの `Mark()` はビューに通知しない**（フラグを書き換えるだけ）。マークを外して画面にも反映したいときは `FileListView::UnmarkPaths(paths)`／`View::UnmarkPaths(model, paths)`（再描画する）を使う。ファイル操作の完了後（コピー）とドロップ後に外すのは、**操作したファイル（`FileOperationCompleted::sources`・ドラッグしたパス）のマークだけ**：そのペインの全マークを外すと、操作の最中に付けたマークと、絞り込みで隠れているマークまで消える。`UnmarkPaths` は、画面の行（`list_`）ではなくモデルの全エントリを見る（隠れたファイルが対象に含まれていても外す）。以前は、コピー完了後の移動元でモデルの `ClearMarks()` を直接呼んでいて、解除したマークが次の再描画（カーソル移動など）まで画面に残る不具合があった（全マークを外す `ClearMarks` / `ClearListMarks` は、呼び出し元が無くなったので削除した）。
- 失敗（ディレクトリが消えた・権限なし）は `std::expected` で返し、モデルもビューも一切変えない。`directory_iterator` の例外を投げる版は、外部で変更された後に使う再読み込みでは落ちるので使わない（エラーコード版）。

## ディレクトリ監視（自動リロード）

各ペインが表示中のディレクトリを監視し、変化を検知したら `Reload()` する。流れ：`pl_watch_directory`（`platform.h`／`osx.mm`。FSEvents のストリーム、コールバックはメインキュー）→ コールバックは `FileListView::OnDirectoryChanged()` で `stale_`／`reload_due_` を立てるだけ → `Application::Update()` のティックから `View::UpdateAutoReload()` → `FileListView::UpdateAutoReload(allow)` が期限を過ぎていれば `Reload()`。判断（まとめる・保留する・間隔をあける）は `FileListView` 側、OS 依存は `pl_watch_directory` の内側に閉じているので、検知の仕組みだけを差し替えられる。

- **FSEvents を選んだ理由（実測済み）**：ディレクトリの fd に張る kqueue／GCD の vnode ソースは、直下のエントリの追加・削除・改名しか検知できず、**既存ファイルへの追記・更新日時・属性の変更を検知できない**（一覧に出しているサイズ・更新日時が古くなる）。FSEvents（`kFSEventStreamCreateFlagFileEvents`）は両方検知でき、遅延は 12〜20ms 程度。代償は、再帰的なので無関係なイベントも届くこと（`$HOME` で 30 秒に 352 件＝毎秒 12 件ほど、直下の子は 0 件）。届いた path を見て**直下の子だけ**を拾い、それ以外は捨てる（`IsRelevantDirEvent`）。
- **監視先は実パスにそろえる**：FSEvents は常に実パス（`/tmp`→`/private/tmp`、`/var/folders`→`/private/var/folders`）で報告するので、`std::filesystem::canonical` した path を渡し、それと比べる。報告された path が想定の形（監視先の配下）でないときは、見逃すよりは変化ありとみなして通知する（安全側）。
- **イベントの取りこぼしや、何が変わったか分からない場合も通知する**：`MustScanSubDirs`／`UserDropped`／`KernelDropped`／`RootChanged`（監視先自身の移動・削除・再作成）／`Mount`／`Unmount`。監視はパス基準なので、監視先が削除されて同じパスに再作成されても、張り直さずに届き続ける。
- **フラグで絞り込まない**：FSEvents のフラグは path ごとに**累積**する（古いファイルへの `setxattr` だけの変更にも `Created|Modified` が付いて届く）ので、「拡張属性だけの変更は無視」のような絞り込みは信用できない。直下の子への変更なら何でも拾う。
- **`FSEventStreamContext` の retain/release に `CFRetain`／`CFRelease` を渡す**（`info` は ObjC オブジェクト `MiataDirWatchState`）。ストリームが解放されるまで `info` が生きる。破棄は `Stop`→`Invalidate`→`Release` の順。`alive` フラグは念のための保護（`Stop`／`Invalidate` の後に FSEvents がコールバックを呼ばないことは実測で確認している）。
- **コールバックの中で `Reload()` しない**：コールバックはフラグを立てるだけ。再スキャンはティック（`UpdateAutoReload`）で行う（監視のコールバックの中から監視自体を破棄することになるのを避け、続く変化を 1 回にまとめるため）。
- **監視は、表示するパスが変わったときだけ張り直す**：`WatchDirectory()` はパス変更の購読側（＝JumpTo／Reload の直後）から呼ばれるが、同じパスを見ている監視が生きていれば何もしない。張り直すと、その間の変更（特に、更新日時に現れない既存ファイルの中身の更新）を取りこぼすため。ストリームは再スキャンの間も生かし続ける。**新しく張るとき**（起動時・ディレクトリの移動時）の、走査から張るまでの隙間の変更は、`FileListModel::ScannedMtime()`（走査の**直前**に取ったディレクトリの更新日時）と現在の更新日時を比べて、食い違っていればもう一度反映待ちにする（追加・削除・改名を確認できる）。
- **自分の走査は、イベントを起こさない**（読むだけ）ので、自動リロードが連鎖することはない。一方、自分の操作（mkdir／rename／コピー・移動など）や、走査が完了する前に届いたイベントの分は、走査の後に届いて、約 0.3 秒後にもう 1 回リロードされることがある（内容は同じで、無害）。
- **ダイアログ表示中は保留する**：`View::UpdateAutoReload()` が `!IsAnyDialogOpened()` を渡す。リネームの入力中に外部でそのファイルが消えると、反映によってカーソルが隣のファイルへ動くため（以前は、確定時の `rename_execute` が、その時点のカーソル位置のエントリを改名したので、**別のファイルを改名してしまった**。いまの `r`（`resources/test.lua`）は、ダイアログを開く前に対象（`cursor_entry`）を決めて、`rename_to(entry, …)` にパスで渡すので、その害は無い。保留は、入力中に一覧が動かないよう、残してある）。`Application::Update()` では `Script::Update()`（コルーチンがダイアログの結果を受けて `rename_to` や `trash` などを呼ぶ）と `CheckDialogState()` の**後**に `UpdateAutoReload()` を呼ぶこと。
- **待ちとスロットル**（`FileListView.mm` の定数）：検知から 300ms 待って反映し、自動リロードどうしは最短 500ms（走査に時間がかかるときは、かかった時間の 8 倍）あける。失敗（ディレクトリが消えた等）は再試行しない（次のイベントか、手動のリロードを待つ）。

## 空のディレクトリ（ファイルもフォルダも1つも無い）

空のディレクトリで、マーク（`mark`/`unmark`/`toggle_mark`、既定では Space）や Enter（`navigate_ok`）を押すと落ちていた。原因は、`View::Mark`/`Unmark`/`ToggleMark` と `NavigateForBrowser` の `Ok` が、`BrowserView::CurrentFileEntryModel()` → `FileListView::GetCurrent()` → `GetEntry(cursorIndex_)` → `*list_[0]` と、**空の `list_` の添字0を読んでいた**こと（未定義動作）。

- **なぜ、いつも落ちるとは限らなかったか**：`Fetch()` は `list_`/`entries_` を `clear()` するだけで、確保した領域は残る。空になった `list_[0]` は、前のディレクトリのエントリがあった領域（解放済みの `FileEntryModel` を指すポインタ）を読む。読んだだけ・解放済みのメモリへ書いただけでは、落ちないことがある（通常のビルドのハーネスでは、修正前でも落ちなかった）。AddressSanitizer（`container-overflow`）で確実に検出できる
- **直し方**：「現在のエントリ」を返す入口を、空のときに `nullptr` を返す形にして、呼び出し側が空を必ず扱うようにした。`FileListView::CurrentOrNull()` を足し、`BrowserView::CurrentFileEntryModel()` と `View::CurrentEntry()` の戻り値を参照からポインタにした。`Mark`/`Unmark`/`ToggleMark` は何もせず、`Ok` は入るものが無いので何もしない。リネーム・`StartFileOperation`（コピー・移動）の、`Size() == 0` による確認も、ポインタの確認に置き換えた（動作は同じ。いまのリネームは、`test.lua` の `r` が `cursor_entry` の nil を見る）。**`GetCurrent()` / `GetEntry()` は削除した**（絞り込みで0行が日常の状態になったので、空のときに範囲外を読む入口を残さない。カーソル下は `CurrentOrNull()`、パスだけなら `CurrentPath()`）
- **空でも動くもの**：カーソルの移動（`Redraw()` が添字を0以上に丸める）、コピー・移動・ゴミ箱（マークが無い）、リネーム、フォルダの作成（空のフォルダにも作れる）、再読み込み・自動リロード（`RestoreCursor` が空を扱う）、ソート、検索、Quick Look、履歴、親へ戻る。絞り込みで0行になった一覧（空のディレクトリと同じ、`list_` が空の状態）も同じ

## 名前が UTF-8 として不正なファイル

ファイル名は、ファイルシステムによっては（ネットワークボリューム、FUSE など）UTF-8 として不正な任意のバイト列になり得る。**APFS は不正な名前を作らせない**（`open(O_CREAT)` が `Illegal byte sequence`（EILSEQ）で失敗する。実測）ので、普通のディスクでは起きない。以前は、そのようなファイルがあるディレクトリを開くと落ちていた。

- **原因**：`pl_normalize_string`（`FileEntryModel` の名前の NFC 化）が、`[[NSString alloc] initWithBytes:… encoding:NSUTF8StringEncoding]` の結果（不正だと nil）をそのまま使い、nil の `UTF8String`（NULL）から `std::string` を作っていた。ここだけ直しても、同じ「一覧を出す」流れの中で**並べ替え**が落ちる：`CaseInsensitiveCompare` は名前を `@(a.c_str())` で NSString にするので、nil を `compare:` の引数にすると例外になる。しかも nil と非 nil で順序が一貫せず、`std::sort` の前提が壊れる。**ゴミ箱**（`pl_trash_file`）も、`fileURLWithPath:nil` の例外で落ちる
- **方針**：**名前（`Name()`/`Basename()`/`Ext()`）は常に UTF-8 として正しい形にする**（`Utf8.h` の `RepairUtf8`：不正な並びを U+FFFD 1つに置き換える。Unicode が勧める「最大の部分列」。正しい名前はそのまま。`pl_normalize_string` は、これで修復してから NFC にする）。そうすれば、並べ替え・検索・描画・リネームの初期値など、名前を NSString にするすべての場所が、nil を考えずに済む。元のバイト列のパスは `Path()` に残り、コピー・移動・リネーム・ゴミ箱・Quick Look・ドラッグは、それを使う（Quick Look とドラッグは、もとから `fileURLWithFileSystemRepresentation:` でバイト列のまま扱う）。`pl_trash_file` は、NSString にできないパスのときだけ、`fileSystemRepresentation` 版の URL を使う（正しいパスは従来どおり）
- **限界**：不正な名前の**フォルダには入れない**。Enter は `Path() / Name()` でパスを作るので、U+FFFD を含む、存在しないパスになり、「移動できませんでした」のダイアログが出る（落ちない。確認済み）。入れるようにするには、ペインのパスが不正なバイトを含んでも壊れないようにする必要がある：ヘッダー（`@(path.c_str())` が nil になって、何も描かない）、ディレクトリ監視（`pl_watch_directory` は NSString にできなければ監視しない）、履歴（`pl_save_string_list` は不正な要素を捨てる。履歴の絞り込みダイアログに出る文字列は未確認）など
- **エラーのダイアログの文面**：ファイル操作のエラーの文面にはパスが入る（「移動できませんでした (パス)」など）。UTF-8 として不正なバイトがあると NSString にできず、`DialogPanel::Impl::MakeLabel` の `labelWithString:nil` が `NSInternalInconsistencyException`（`Invalid parameter not satisfying: stringValue != nil`）で落ちる（実測）。UI からは作れない（Enter は修復した名前でパスを作る）が、Lua の `jump_to` に不正なバイトを含むパスを渡すと起きた（履歴の機能からあった）。ファイル操作のエラーは必ず `View::ReportFileError` を通るので、そこで文面を `RepairUtf8` に通す。**Lua から渡す `dialog_*` の文字列は、修復しない**（`Dialog.mm` の `@(x.c_str())` の 6 か所は、不正だと nil になり得る。スクリプトの書き手の責任。UI からは作れない）
- **NSString の癖（実測）**：`initWithBytes:encoding:NSUTF8StringEncoding` と `stringWithUTF8String:` は、**単独の `A9`（Latin-1 の ©）だけ**、厳密には不正なのに、位置によらず受け付ける（80〜BF のほかの単独の継続バイトは拒否する）。`RepairUtf8` は厳密に判定するので、`A9` を含む名前は `�` になる（以前は `©` に見えていた）。逆に、厳密には正しいのに NSString が拒否する並びは無い（1〜4バイトの総当たりで確認）ので、修復した名前は必ず NSString にできる
