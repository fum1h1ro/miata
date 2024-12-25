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
  end
end

namespace :build do
  desc "Build Debug"
  task :debug => ["cmake:debug"] do
    cd BUILD_DIR_DEBUG do
      sh "make"
    end
  end
  desc "Build Release"
  task :release => ["cmake:release"] do
    cd BUILD_DIR_RELEASE do
      sh "make"
    end
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

