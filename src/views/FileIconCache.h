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
// カスタムアイコン・リンクの矢印バッジ)。ただし、ダウンロード前のファイル(クラウドストレージのプレースホルダ。
// IsDataless())と、UTF-8として不正なパスは、ファイルに触れず、拡張子の種類のアイコンにする(Resolve参照)。
// 引くのは、描くとき(見えている行だけ)に、保持していないパスだけ。保持していれば、パスの文字列の検索だけ。
// 実測値と、設計の決定・罠は .claude/rules/file-list.md。
namespace miata::views {
    class FileIconCache {
    public:
        // 全ペインで1つ。同じフォルダを左右に出すことが多いので、共有する
        static FileIconCache& Instance()
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

        // 保持する件数の上限。1件は、描いた後でおよそ10KBなので、上限でも20MBほど
        static constexpr size_t kMaxIcons = 2048;

    private:
        FileIconCache() = default;

        // 描く大きさの、1枚の画像にする。NSWorkspaceが返す画像は、いくつもの大きさの絵を持っていて、描くたびに、
        // 合う絵を選んで縮める(保持した後でも、書類で数十µs、.appの実物で百数十µsかかる)。描く大きさごとに、
        // 1回だけ描いて、画像として保持させれば、以降の描画は、その数分の1で済む。倍率(画素密度)や色空間が
        // 変われば、AppKitが描き直す
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

        static NSImage* Resolve(const models::FileEntryModel& entry)
        {
            NSWorkspace* workspace = [NSWorkspace sharedWorkspace];

            // ダウンロード前のファイル(クラウドストレージのプレースホルダ)には、iconForFile:を呼ばない。カスタムアイコンや、
            // .appの中のInfo.plistを読みに行って、ダウンロードを起こすかもしれない(未検証)。一覧は、メタデータだけを読む
            // (<CLOUD>の札と同じ)。印は、フォルダやエイリアスにも付くことがあり、そのときも同じ。
            // パスは、元のバイト列で引く(RepairUtf8を通すと、別のファイルを指す)。UTF-8として不正なパスは、
            // NSStringにならない(nil)ので、iconForFile:に渡さない
            NSString* path = entry.IsDataless() ? nil : @(entry.Path().c_str());
            NSImage* icon = path ? [workspace iconForFile:path] : nil;
            return icon ? icon : [workspace iconForContentType:TypeOf(entry)];
        }

        // ファイルに触れずに(名前と、種類の判定だけで)決める、種類のアイコンの型。順は、フォルダ → エイリアス → 拡張子
        // → シンボリックリンク。フォルダは、リンク先がフォルダのリンクも(IsDirectory()はリンクをたどる)。右の欄の札
        // (SizeColumn)は、リンクを先に見る(リンクは、札で <LNK> と分かるので、アイコンは、リンク先の種類に合わせる)
        static UTType* TypeOf(const models::FileEntryModel& entry)
        {
            // Ext()は、先頭の「.」を含む(「.txt」)。「.gitignore」のような、先頭だけがドットの名前では空(拡張子なし)。
            // UTF-8として正しい(@()がnilにならない)
            const std::string& dotted = entry.Ext();
            NSString* ext = dotted.size() > 1 ? @(dotted.c_str() + 1) : nil;
            UTType* type = nil;

            if (entry.IsDirectory()) {
                // .app・.framework のような、パッケージとして知られている拡張子だけ使う。
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
