#ifndef PLATFORM_H__
#define PLATFORM_H__

#include <map>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>
#include <expected>
#include <functional>
#include <memory>
#include <cstdint>
#include "FileError.h"

struct Color4f {
    float r = 0, g = 0, b = 0, a = 1;
};

enum class pl_color_type {
    label_color,
    secondary_label_color,
    tertiary_label_color,
    quaternary_label_color,
    text_color,
    placeholder_text_color,
    selected_text_color,
    text_background_color,
    selected_text_background_color,
    keyboard_focus_indicator_color,
    unemphasized_selected_text_color,
    unemphasized_selected_text_background_color,
    link_color,
    separator_color,
    selected_content_background_color,
    unemphasized_selected_content_background_color,
    selected_menu_item_text_color,
    //selected_menu_item_color,
    header_text_color,
    alternating_content_background_colors,
    control_accent_color,
    control_color,
    control_background_color,
    control_text_color,
    disabled_control_text_color,
    //current_control_tint,
    selected_control_color,
    //secondary_selected_control_color,
    //alternate_selected_control_color,
    selected_control_text_color,
    alternate_selected_control_text_color,
    scrubber_textured_background_color,
    window_background_color,
    window_frame_text_color,
    //window_frame_color,
    under_page_background_color,
    find_highlight_color,
    highlight_color,
    shadow_color,
    quaternary_system_fill_color,
    quinary_label_color,
    quinary_system_fill_color,
    secondary_system_fill_color,
    system_fill_color,
    tertiary_system_fill_color,
    text_insertion_point_color,
};

// KeyBinding::Key::mods_ で使うモディファイアのビット表現。NSEventModifierFlagsから
// osx.mm 側で変換して渡す(値そのものはAppKitのビット位置とは無関係な自前定義)。
namespace pl_modifier {
    constexpr uint16_t Shift = 1 << 0;
    constexpr uint16_t Ctrl  = 1 << 1;
    constexpr uint16_t Alt   = 1 << 2;
    constexpr uint16_t Super = 1 << 3;
}

// NFCに正規化する。UTF-8として不正なバイトは、U+FFFDに置き換える(結果は常にUTF-8として正しい)。
std::string pl_normalize_string(const std::string& input);
// ファイル名の比較用の鍵: NFCに正規化して、大文字小文字を畳む。大文字小文字・正規化を区別しないボリューム(APFSの既定)で、
// 同じ場所に置くと同じ名前になる2つの名前は、同じ鍵になる。区別するボリュームでは、別の名前でも同じ鍵になりうる(安全側:
// 名前の重なりを断る検査に使う)。UTF-8として不正なバイトはU+FFFDになる(不正な名前どうしも、同じ鍵になりうる)。
std::string pl_name_collation_key(const std::string& name);
void pl_play_beep();

// AppKitのcontentView(NSView*)を(__bridge void*)で返す。
// views/ 層はAppKit型に直接依存させたくないため、このvoid*橋渡し越しに使う。
void* pl_get_content_view();

// メインウィンドウを生成して表示する。
void pl_create_main_window(int width, int height, const char* title);

// メインウィンドウ自体の背景色。ペインやヘッダーは自分で塗るので、ここで決まるのは、その隙間(ペインの
// 境目)と、リサイズ中に広がった部分に見える色。alphaは使わず、不透明に塗る。設定の読み込みの後に呼ぶ
// (pl_create_main_windowの時点では、まだ設定が読まれていない)。
void pl_set_window_background_color(const Color4f& color);

// キーコードはmacOSのvirtual keycode(NSEvent.keyCode)そのもの。
// modsはpl_modifier::* のビットOR。
void pl_set_key_down_handler(std::function<void(uint16_t keycode, uint16_t mods)> handler);
void pl_set_key_up_handler(std::function<void(uint16_t keycode, uint16_t mods)> handler);
void pl_set_resize_handler(std::function<void(int width, int height)> handler);
// 一定間隔(seconds)で呼ばれる更新タイマーを開始する。
void pl_start_timer(double interval_seconds, std::function<void()> callback);

// pl_watch_directory() が返すディレクトリ監視のハンドル。破棄すると監視を止める。
class pl_dir_watch {
public:
    virtual ~pl_dir_watch() = default;
};
// dir直下のエントリの変化(追加・削除・改名、既存ファイルの中身・更新日時・属性の更新)を監視し、
// 検知するたびにon_changeをメインスレッドで呼ぶ(短時間の変化はまとめて1回になることがある)。
// より深い階層の変化は通知しない。dir自身が移動・削除された場合も通知する(その後、同じパスに
// 再作成されても通知は続く)。ネットワークボリュームなど、他のマシンからの変更が通知されない場所もある。
// dirが存在しない・権限が無いなど、監視できない場合はnullptrを返す。
std::unique_ptr<pl_dir_watch> pl_watch_directory(const std::filesystem::path& dir, std::function<void()> on_change);

// 実行ファイルをPATH、および主要なインストール先(Homebrew等)から探す。見つからなければnullopt。
// GUIアプリはログインシェルのPATHを継承しないことが多いため、PATHだけに頼らない。
std::optional<std::filesystem::path> pl_find_executable(const std::string& name);

struct ProcessRunResult {
    int exit_code = -1;
    std::string stdout_text;
};
// executableをargsで起動し、標準入力をstdin_fileから読み込ませ、標準出力を回収する(標準エラーは捨てる。環境変数は継承する)。
// 起動そのものに失敗した場合と、timeout_seconds以内に終わらなかった場合(プロセスは強制終了する)のみ unexpected を返す
// (プロセスが非0で終了しても失敗扱いにはしない。呼び出し側がexit_codeを見て判断する。シグナルで終了したときは128 + シグナル番号)。
// 終わるまで呼び出し元のスレッドを止めるが、実行ループは回さない(NSTask::waitUntilExitは、待つ間に実行ループを回すので、
// 数msで終わる子プロセスでも1回に約80msかかり、その間にタイマーが割り込んで、呼び出し元の更新の途中に再入する)。
std::expected<ProcessRunResult, std::string> pl_run_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args,
    const std::filesystem::path& stdin_file,
    double timeout_seconds = 10.0
);

struct CustomDialogCheckbox {
    std::string label;
    bool checked;
};
// 縦に並んだ選択肢の行。行でEnter(またはクリック)すると、その場でダイアログが閉じて選択が確定する。
// options は1件以上、selected は範囲内(Application.cc が Lua からの入力を検証してから作る)。
struct CustomDialogSelect {
    std::vector<std::string> options;
    int selected = 0; // 0-based。最初にカーソルがある行
};
struct CustomDialogSpec {
    std::string title;
    std::string message; // informative text (optional)
    std::vector<std::string> buttons;
    std::vector<CustomDialogCheckbox> checkboxes;
    std::optional<CustomDialogSelect> select; // 1ダイアログに1つ
};
// ダイアログが閉じた理由は、ボタン(button_index)か、選択リストの行(select_index)のどちらか一方だけ。
struct CustomDialogResult {
    std::optional<int> button_index; // 0-based。ボタンで閉じたとき
    std::optional<int> select_index; // 0-based。行で閉じたとき
    std::vector<bool> checkboxes;
};

// ファイルをゴミ箱へ移す。失敗(権限が無い・消えた等)はエラーで返す。権限が無い失敗は
// FileError::permission_deniedで分かる。
// クラウドストレージ(下のpl_is_in_file_provider_domain)の中で、権限の失敗になったときは、Finderに頼み直す
// (pl_trash_file_via_finder)。その場合の失敗の説明は、Finderに頼んだものになる。
std::expected<void, miata::FileError> pl_trash_file(const std::filesystem::path& path);

// pathが、ファイルプロバイダ(Dropbox・Google Driveなど)の領域の中か。サードパーティのファイルプロバイダの領域は、
// ~/Library/CloudStorage/ の下のフォルダ(Dropbox-Personal など)で、その中身が対象。領域そのもののフォルダと、
// ~/Library/CloudStorage/ 直下の項目は false。親フォルダを実パスにして比べる(~/Dropbox のような、領域へのシンボリック
// リンク経由のパスも、領域の中と分かる。path 自身がリンクでも、リンクそのものを見る)。iCloud Drive(~/Library/Mobile Documents)
// は対象外(未検証)。
bool pl_is_in_file_provider_domain(const std::filesystem::path& path);

// pathが、クラウドストレージの中か。~/Library/CloudStorage(Dropbox・Google Drive・OneDriveなどのファイルプロバイダの領域)と、
// ~/Library/Mobile Documents(iCloud Drive)の、実パスの下(領域そのものも含む)。親フォルダの実パスと、path自身の実パス
// (シンボリックリンクなら、リンク先)の、どちらかが下にあれば真(クラウドの中への、シンボリックリンクも真。
// ~/Dropbox のような、領域へのリンク経由のパスも真)。メタデータ(realpath)だけを見て、中身は読まない。
// 名前の頭のアイコン(FileIconCache)が、iconForFile:(カスタムアイコンや .app の中の Info.plist を読みかねないので、
// ダウンロードを起こすかもしれない)を呼んでよいかの判断に使う。pl_is_in_file_provider_domainと違い、領域そのものと、
// iCloud Driveも対象。調べられないとき(存在しない・クラウドストレージを使っていない)は false。
bool pl_is_in_cloud_storage(const std::filesystem::path& path);

// Finderに頼んで、ファイルをゴミ箱へ移す(Apple Events)。Finder自身の権限で行われる。pl_trash_fileのフォールバック
// (クラウドストレージの中では、普段の起動のアプリからは、書き込みの許可があっても、ゴミ箱への移動だけが拒否される。
// .claude/rules/permissions-signing.mdの「権限エラーの案内」)。初回は、オートメーションの許可のダイアログが出て、答えるまで呼び出しが止まる
// (メインスレッドで呼ぶと、その間Miata全体が止まる)。失敗(許可が無い・Finderが応答しない・Finderが断った)はFileErrorで返す。
// permission_deniedは常にfalse(フルディスクアクセスの案内は当てはまらない。メッセージが、オートメーションの許可を案内する)。
// シンボリックリンクは頼まずに失敗にする(Finderにファイルのurlで頼むと、返事が来ない。実測)。
std::expected<void, miata::FileError> pl_trash_file_via_finder(const std::filesystem::path& path);

// pathがFinderのエイリアス(「エイリアスを作成」で作る、リンク先を指す通常のファイル)か。Finder情報の「エイリアス」の印(kIsAlias)を見る。
// シンボリックリンクは含まない(リンクそのものを見るので、false。NSURLIsAliasFileKeyは、シンボリックリンクにも真を返すので使わない)。
// リンク先は見ない(壊れたエイリアスでも true)。調べられないとき(存在しない・権限が無い・Finder情報を持たないボリューム)は false。
bool pl_is_alias_file(const std::filesystem::path& path);

// pathが、ダウンロード前のファイル(クラウドストレージのプレースホルダ。大きさなどは分かるが、中身がローカルに無い)か。
// Dropbox・iCloud Driveなどのファイルプロバイダの中で、OSがファイルに立てる SF_DATALESS(st_flags)を見る。
// メタデータを読むだけなので、ダウンロードは起きない(実測)。中身を読む操作(開く・Quick Look・コピー)では起きる(OSの仕組み。未確認)。
// シンボリックリンクはリンクそのものを見る(ローカルのリンクに、この印は付かない。実測)。フォルダにも付くことがあるが、
// 意味が違う(中身の一覧をまだ取っていない。一覧すると外れる)。調べられないとき(存在しない・権限が無い)は false。
bool pl_is_dataless_file(const std::filesystem::path& path);

// pathに、Finderの「隠す」フラグ(chflags hidden。st_flags の UF_HIDDEN)が付いているか。~/Library・/usr・/bin などに付いている。
// 名前の頭の "." は見ない(それは FileEntryModel::IsHidden)。メタデータを読むだけ(lstat 1回)。シンボリックリンクは、
// リンクそのもののフラグを見る。調べられないとき(存在しない・権限が無い)は false。
bool pl_is_hidden_file(const std::filesystem::path& path);

// ファイル1つをコピーする(copyfile(3))。Finderのコピーと同じく、中身に加えて、更新日時・権限・拡張属性・ACLも引き継ぐ。
// srcがシンボリックリンクなら、たどらずに、リンクそのものを写す(壊れたリンクも写せる)。APFSの同じボリュームの中では、
// クローン(中身を共有する、瞬間のコピー)になる(別のボリュームやAPFS以外では、普通のコピーに戻る)。
// replaceが true なら、dstに同名があれば置き換える: 同じフォルダの一時ファイル(".miata-copy-…")へ置いて、全部成功したときだけ、
// 名前を付け替える(rename)。途中で失敗しても(元が読めない・容量が足りない・先が外れた)、dstの元の内容は残る。dstがリンクなら、
// リンク先ではなく、リンクそのものを置き換える。別のハードリンクの中身は変わらない。dstがフォルダなら、EISDIRで失敗する(消さない)。
// false なら、同名があると EEXISTで失敗する(上書きしない)。失敗したとき、作りかけのファイルは残らない。
// 渡してよいのは、普通のファイルとシンボリックリンクだけ。フォルダを渡すと、中身は写さずに、空のフォルダを作る。FIFO・デバイスなどを
// 渡すと、デバイスなら読み出して普通のファイルにしてしまう(/dev/null で、空のファイルができた)ので、呼ぶ側が先に除くこと。
// on_progress: 中身をコピーしている間、1 MiB ほどごとに、このファイルでこれまでにコピーしたバイト数(累計)で呼ぶ(コピーしているスレッドで)。
// クローン(瞬間のコピー)・リンクのときは、呼ばない(呼ばれずに、終わる)。nullptr でもよい。
// 失敗はerror_code(errno)で返す。
std::error_code pl_copy_file(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    bool replace,
    const std::function<void(std::int64_t copied)>& on_progress = nullptr
);

// フォルダの属性(権限・更新日時・BSDフラグ・拡張属性・ACL)だけを、srcからdstへ写す。中身は写さない。dstは存在するフォルダ。
// 更新日時は、dstの中身が変わるたびに書き換わるので、中身を置き終えた後に呼ぶこと。読み取り専用の権限も、置き終えてから付けること。
std::error_code pl_copy_directory_attributes(const std::filesystem::path& src, const std::filesystem::path& dst);

// システム設定の「プライバシーとセキュリティ > フルディスクアクセス」を開く。OSの保護で止められたファイル操作を、
// 許可してもらうための案内用。その画面を直接開けなければ、システム設定そのものを開く。何かを開けたらtrue。
// 許可を与えるのはユーザーで、アプリからは変えられない(許可した後、アプリの起動し直しが要る場合がある)。
bool pl_open_full_disk_access_settings();

// アプリの指定から、アプリ(.app)のパスを探す。見つからなければnullopt。指定は次のどれか:
//  - 絶対パス("/" で始まる): そのパスがフォルダ(.appバンドル)として存在すれば、そのまま
//  - Bundle ID(例: "com.apple.TextEdit"): LaunchServicesに登録されたアプリ
//  - 名前(例: "Visual Studio Code"。".app" は付けても省いてもよく、大文字小文字は区別しない): 標準の場所
//    (~/Applications、/Applications、/Applications/Utilities、/System/Applications、/System/Applications/Utilities、
//    /System/Library/CoreServices)を、この順に探す
std::optional<std::filesystem::path> pl_find_application(const std::string& spec);

// pathsを開く(NSWorkspace。Finderのダブルクリックと同じ)。appが空なら、ファイルごとの既定のアプリで開く(フォルダはFinder、
// .appバンドルは起動)。空でなければ、そのアプリ(pl_find_application)で全部をまとめて開く。pathsは空でないこと。
// 要求を出す前に分かる失敗(アプリが見つからない)は、unexpectedで説明を返す。要求のあとで分かる失敗(開くアプリが無い、
// ファイルが無いなど)は、全部の結果が出た後に、メインスレッドでon_failureを1回だけ呼ぶ(failed件が、total件中。
// messageは、OSの説明の1件)。成功したときは何も呼ばない。
std::expected<void, std::string> pl_open_paths(
    const std::vector<std::filesystem::path>& paths,
    const std::string& app,
    std::function<void(int failed, int total, const std::string& message)> on_failure
);

// Finderで、pathsを選択した状態で表示する(複数は、1つのウィンドウにまとめる)。
void pl_reveal_paths(const std::vector<std::filesystem::path>& paths);

// テキストをクリップボード(一般のペーストボード)に置く。UTF-8として不正なバイトは、U+FFFDにして置く。置けたらtrue。
bool pl_set_clipboard_text(const std::string& text);

// アプリの設定(NSUserDefaults。ウィンドウの位置・サイズの自動保存と同じ保存先)に、keyごとの文字列の配列を
// 保存する/読む。読み出しは失敗しない: キーが無い・配列でない場合は空、文字列でない要素は読み飛ばす。保存は
// 呼んだ時点でOSに渡り、ディスクへはOSが書く(終了時の処理は要らない)ので、変わるたびに呼んでよい。
// 空の配列はキーを消す。UTF-8として不正な要素は保存しない。
void pl_save_string_list(const std::string& key, const std::vector<std::string>& values);
std::vector<std::string> pl_load_string_list(const std::string& key);
// 文字列から文字列への辞書(1つのkeyに、名前をつけたいくつかの値)を、同じ保存先に保存する/読む。保存の性質は
// pl_save_string_listと同じ(変わるたびに呼んでよい)。読み出しは失敗しない: キーが無い・辞書でない場合は空、文字列でない
// 名前や値は読み飛ばす。空の辞書はキーを消す。UTF-8として不正な名前や値は、その項目だけ保存しない(位置で意味を決める
// 配列と違い、1つ抜けても、ほかの項目の意味は変わらない)。
void pl_save_string_map(const std::string& key, const std::map<std::string, std::string>& values);
std::map<std::string, std::string> pl_load_string_map(const std::string& key);

std::filesystem::path pl_find_font_filename(const std::string& font_name);
std::filesystem::path pl_get_home_dir();
std::filesystem::path pl_get_config_dir();
std::string pl_read_file(const char* path);
std::expected<std::string, std::string> pl_read_resource_file(const char* path);

Color4f pl_get_color(pl_color_type type);

#endif // PLATFORM_H__
