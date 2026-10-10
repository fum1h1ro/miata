#ifndef VIEWS_FILE_ICON_CACHE_H__
#define VIEWS_FILE_ICON_CACHE_H__

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string>
#include <unordered_map>
#include "../models/FileEntryModel.h"

// 一覧の、ファイル名の頭に出すアイコンを、パスごとに保持する。AppKit依存のヘルパーなので.mmファイルからのみ
// includeする(ViewMetrics.hと同じ規約)。メインスレッドからだけ使う。
//
// アイコンは、Finderと同じもの(NSWorkspaceのiconForFile:の結果。拡張子の種類のアイコン・.appの実物のアイコン・
// カスタムアイコン・リンクの矢印バッジ)。引くのは、描くとき(見えている行だけ)に、保持していないパスだけ:
// 1回は、ふつうのファイルで50µs、.appで0.1〜0.2msほど(実測)。保持していれば、パスの文字列の検索だけ。
namespace miata::views {
    class FileIconCache {
    public:
        // 全ペインで1つ。同じフォルダを左右に出すことが多いので、共有する
        static FileIconCache& Shared()
        {
            static FileIconCache cache;
            return cache;
        }

        // entryのアイコン(pointsは、描く大きさ。一辺のpt)。保持していなければ、ここで引いて保持する(OSが返すかぎり、
        // nilにならない。nilでも、描画は何もしないだけ)。描くときは、pointsの正方形に、そのまま(拡大縮小なしで)描く
        NSImage* IconFor(const models::FileEntryModel& entry, CGFloat points)
        {
            // 大きさが変わったら、作り直す(大きさはフォントサイズで決まり、フォントサイズは起動時にだけ決まるので、
            // ふつうは起きない)
            if (points != points_) {
                icons_.clear();
                points_ = points;
            }

            // キーはパスのバイト列(エントリのポインタは、再スキャンのたびに作り直されるので使えない)
            std::string key = entry.Path().native();
            if (auto it = icons_.find(key); it != icons_.end()) return it->second;

            // 上限を超えたら、全部捨てる。画面に出る行(数十)よりずっと大きいので、捨てた直後に引き直すのは、
            // 見えている行だけ。数万件のフォルダを端から端までスクロールしても、メモリは上限で止まる
            if (icons_.size() >= kMaxIcons) icons_.clear();

            NSImage* icon = Fit(Resolve(entry), points);
            icons_.emplace(std::move(key), icon);
            return icon;
        }

        // 保持しているものを全部捨てる。別のフォルダへ移ったとき(引き直す。アイコンが変わっていても、移れば反映される)
        void Clear()
        {
            icons_.clear();
        }

        // 保持している件数(テスト用)
        size_t Size() const
        {
            return icons_.size();
        }

        // 保持する件数の上限。1件は、描いた後でおよそ10KB(実測: 2000件で20MB)なので、上限でも20MBほど
        static constexpr size_t kMaxIcons = 2048;

    private:
        FileIconCache() = default;

        // 描く大きさの、1枚の画像にする。NSWorkspaceが返す画像は、いくつもの大きさの絵を持っていて、描くたびに、
        // 合う絵を選んで縮める。保持した後でも、1つ描くのに、書類で50µs、.appの実物で150µsかかる(実測: 60行で、
        // 書類3ms・.app 8〜12ms)。描く大きさ(と、倍率・外観)ごとに、1回だけ描いて、画像として保持させれば、
        // 以降は、60行で1.1msで済む。倍率や外観が変われば、AppKitが描き直す
        static NSImage* Fit(NSImage* source, CGFloat points)
        {
            if (!source) return nil;
            return [NSImage imageWithSize:NSMakeSize(points, points)
                                  flipped:NO
                           drawingHandler:^BOOL(NSRect rect) {
                               NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;
                               [source drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
                               return YES;
                           }];
        }

        NSImage* Resolve(const models::FileEntryModel& entry) const
        {
            NSWorkspace* workspace = [NSWorkspace sharedWorkspace];

            // ダウンロード前のファイル(クラウドストレージのプレースホルダ)には、iconForFile:を呼ばない。カスタムアイコンや、
            // .appの中のInfo.plistを読みに行って、ダウンロードを起こすかもしれない(未検証)。一覧は、メタデータだけを読む
            // (<CLOUD>の札と同じ)。このときは、拡張子の種類のアイコンにする(ファイルには触れない)
            if (!entry.IsDataless()) {
                // 元のバイト列のパスで引く(RepairUtf8を通すと、別のファイルを指す)。UTF-8として不正なパスは、
                // NSStringにならない(nil)ので、種類のアイコンにする
                NSString* path = @(entry.Path().c_str());
                if (path) {
                    if (NSImage* icon = [workspace iconForFile:path]) return icon;
                }
            }
            return [workspace iconForContentType:TypeOf(entry)];
        }

        // 名前の、最後の「.」より後ろ。「.gitignore」は gitignore(LaunchServicesの数え方に合わせる)。無ければnil
        static NSString* ExtensionOf(const std::string& name)
        {
            auto dot = name.rfind('.');
            if (dot == std::string::npos || dot + 1 >= name.size()) return nil;
            // Name()はUTF-8として正しく、「.」はASCIIなので、後ろも正しい(nilにならない)
            return [[NSString alloc] initWithBytes:name.data() + dot + 1
                                            length:name.size() - dot - 1
                                          encoding:NSUTF8StringEncoding];
        }

        // ファイルに触れずに(名前と、種類の判定だけで)決める、種類のアイコンの型
        static UTType* TypeOf(const models::FileEntryModel& entry)
        {
            NSString* ext = ExtensionOf(entry.Name());
            UTType* type = nil;

            if (entry.IsDirectory()) {
                // フォルダ。.app・.framework のような、パッケージとして知られている拡張子だけ使う。
                // 「foo.zip」というフォルダに、zipのアイコンは付けない(動的な型になる)
                if (ext) type = [UTType typeWithFilenameExtension:ext conformingToType:UTTypeDirectory];
                return (type && !type.isDynamic) ? type : UTTypeFolder;
            }
            if (entry.IsAlias()) return UTTypeAliasFile;

            if (ext) type = [UTType typeWithFilenameExtension:ext];
            // 知らない拡張子は、1つずつ別の動的な型になる(1000種類で1000個の別のアイコンになる)ので、まとめる
            if (type && !type.isDynamic) return type;
            return entry.IsSymlink() ? UTTypeSymbolicLink : UTTypeData;
        }

        std::unordered_map<std::string, NSImage*> icons_;
        CGFloat points_ = 0; // icons_ の画像の、描く大きさ
    };
}

#endif // VIEWS_FILE_ICON_CACHE_H__
