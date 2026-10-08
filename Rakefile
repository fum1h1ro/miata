require 'rake/clean'

BUILD_DIR = "_build"
BUILD_DIR_DEBUG = "#{BUILD_DIR}/debug"
BUILD_DIR_RELEASE = "#{BUILD_DIR}/release"
BUILD_DIR_XCODE = "#{BUILD_DIR}/xcode"

CLEAN.include(BUILD_DIR)
CLOBBER.include(BUILD_DIR)

directory BUILD_DIR
directory BUILD_DIR_DEBUG
directory BUILD_DIR_RELEASE
directory BUILD_DIR_XCODE

# 署名(任意)。環境変数 MIATA_CODESIGN_IDENTITY に、署名に使う証明書(`security find-identity -v -p codesigning` に出る
# SHA-1 ハッシュか、名前)を入れておくと、ビルドのたびに(make のあとで毎回)、その証明書で .app に署名する。
# 無ければ、これまでどおり、アドホック署名(リンカーがつけるもの)のまま。
#
# なぜ: アドホック署名だと、フルディスクアクセスなどの許可(TCC)が、実行ファイルのハッシュに結び付くので、ビルドし直すたびに
# 外れる(別のアプリとして扱われる)。証明書で署名すると、許可は、アプリの ID + 証明書の署名の要件に結び付くので、ビルドし直しても
# 残る。毎回署名し直すのは、make が、リソース(resources/ の Lua など)だけをコピーし直したときも、署名が古くならないようにするため。
#
# 証明書の名前・チーム ID は、リポジトリに書かない(環境変数で渡す)。コマンドも、出力に出さない(verbose: false)。
# hardened runtime は付けない(テストが DYLD_INSERT_LIBRARIES を使うため。配布用の署名・公証は、まだ組み込んでいない)。
def sign_app(build_dir)
  identity = ENV["MIATA_CODESIGN_IDENTITY"].to_s.strip
  return if identity.empty?

  app = File.join(build_dir, "Miata.app")
  sh "codesign", "--force", "--sign", identity, "--timestamp=none", app, verbose: false do |ok, _|
    abort "codesign failed: check MIATA_CODESIGN_IDENTITY (usable identities: security find-identity -v -p codesigning)" unless ok
  end
  sh "codesign", "--verify", "--strict", app, verbose: false
  puts "signed #{app} (identity from MIATA_CODESIGN_IDENTITY)"
end

namespace :cmake do
  desc "Generate Debug build files"
  task :debug => [BUILD_DIR_DEBUG] do
    cd BUILD_DIR_DEBUG do
      sh "cmake -DCMAKE_BUILD_TYPE=Debug ../.."
    end
  end
  desc "Generate Release build files"
  task :release => [BUILD_DIR_RELEASE] do
    cd BUILD_DIR_RELEASE do
      sh "cmake -DCMAKE_BUILD_TYPE=Release ../.."
    end
  end
  desc "Generate Xcode build files"
  task :xcode => [BUILD_DIR_XCODE] do
    cd BUILD_DIR_XCODE do
      sh "cmake -G Xcode ../.."
      sh "open ."
    end
  end
end


desc "Build all"
task :build do
  [BUILD_DIR_DEBUG, BUILD_DIR_RELEASE].each do |dir|
    next unless Dir.exist?(dir)
    cd dir do
      sh "make"
    end
    sign_app(dir)
  end
end

namespace :build do
  desc "Build Debug"
  task :debug => ["cmake:debug"] do
    cd BUILD_DIR_DEBUG do
      sh "make"
    end
    sign_app(BUILD_DIR_DEBUG)
  end
  desc "Build Release"
  task :release => ["cmake:release"] do
    cd BUILD_DIR_RELEASE do
      sh "make"
    end
    sign_app(BUILD_DIR_RELEASE)
  end
end


namespace :run do
  desc "Run Debug"
  task :debug => ["build:debug"] do
    sh "#{BUILD_DIR_DEBUG}/miata.app/Contents/MacOS/miata"
  end
  desc "Run Release"
  task :release => ["build:release"] do
    sh "#{BUILD_DIR_RELEASE}/miata.app/Contents/MacOS/miata"
  end
end


# アプリアイコン: 元画像 assets/AppIcon.png(正方形のPNG)を、resources/AppIcon.icns に変換する。
# 元画像は resources/ の外に置く(resources/ の中身は、全部 .app の Contents/Resources/ にコピーされるため)。
# .icns は resources/ に置いてコミットする(ビルドに必要。CMakeLists.txt の MACOSX_BUNDLE_ICON_FILE で
# Info.plist に登録している)。差し替えるときは、assets/AppIcon.png を置き換えて、rake icon → ビルドし直す。
ICON_SOURCE = "assets/AppIcon.png"
ICON_FILE = "resources/AppIcon.icns"

desc "Build #{ICON_FILE} from #{ICON_SOURCE} (a square PNG, 1024x1024 recommended)"
task :icon do |_t, args|
  require 'shellwords'
  require 'tmpdir'
  # 変換するのはいつも ICON_SOURCE。以前の rake 'icon[x.png]' の引数を黙って無視すると、x.png を変換した
  # つもりで別の画像から .icns ができてしまうので、エラーにする
  abort "rake icon takes no arguments (it always converts #{ICON_SOURCE}): put your image there" unless args.extras.empty?

  png = ICON_SOURCE
  abort "source image not found: #{png}" unless File.file?(png)

  w, h = %w[pixelWidth pixelHeight].map { |key| `sips -g #{key} #{png.shellescape}`[/#{key}: (\d+)/, 1].to_i }
  abort "not a readable image: #{png}" if w.zero? || h.zero?
  abort "the icon must be square (got #{w}x#{h})" unless w == h
  warn "warning: #{w}x#{h} is smaller than 1024x1024, so the larger sizes are upscaled" if w < 1024

  Dir.mktmpdir do |tmp|
    iconset = File.join(tmp, "AppIcon.iconset")
    mkdir_p iconset, verbose: false
    # iconutil が要求するファイル名と、そのピクセル数(@2x は Retina 用で2倍)
    {
      "icon_16x16" => 16,     "icon_16x16@2x" => 32,
      "icon_32x32" => 32,     "icon_32x32@2x" => 64,
      "icon_128x128" => 128,  "icon_128x128@2x" => 256,
      "icon_256x256" => 256,  "icon_256x256@2x" => 512,
      "icon_512x512" => 512,  "icon_512x512@2x" => 1024,
    }.each do |name, size|
      sh "sips -z #{size} #{size} #{png.shellescape} --out #{File.join(iconset, "#{name}.png").shellescape} > /dev/null", verbose: false
    end
    sh "iconutil -c icns #{iconset.shellescape} -o #{ICON_FILE}"
  end
  puts "wrote #{ICON_FILE} from #{png} (rebuild to apply: rake build:debug)"
end
