//
//  main.cpp
//  Asimov Watcher
//
//  Created for the Asimov project.
//

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

struct Rule {
  std::string key;
  std::string value;
};

class AsimovWatcher {
public:
  AsimovWatcher(const std::string &watchPath,
                const std::vector<std::string> &ignores)
      : m_watchRoot(watchPath) {

    // Rules configuration
    m_sentinels = {{"package.json", "node_modules"},
                   {"composer.json", "vendor"},
                   {"requirements.txt", "venv"},
                   {"Gemfile", "vendor"},
                   {"Cargo.toml", "target"}};

    // Pre-calculate absolute ignore paths for efficient checking
    m_absoluteIgnorePrefixes.reserve(ignores.size());
    for (const auto &ignoreDir : ignores) {
      if (ignoreDir.empty())
        continue;

      // Construct absolute path: watchRoot / ignoreDir
      fs::path absPath = m_watchRoot / ignoreDir;
      std::string absPathStr = absPath.string();

      // Ensure the path string ends with a separator for robust prefix matching
      // unless it's the root itself (unlikely for an ignore rule)
      if (absPathStr.back() != fs::path::preferred_separator) {
        absPathStr += fs::path::preferred_separator;
      }

      m_absoluteIgnorePrefixes.push_back(absPathStr);
    }
  }

  void run() {
    std::cout << "DEBUG: Asimov Watcher started on " << m_watchRoot.string()
              << std::endl;
    std::cout << "DEBUG: Ignoring directories: ";
    for (const auto &dir : m_absoluteIgnorePrefixes)
      std::cout << dir << ", ";
    std::cout << std::endl;

    // 1. Initial Scan (Async background)
    dispatch_async(
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
          std::cout << "DEBUG: Starting initial scan..." << std::endl;
          try {
            scanRecursive(m_watchRoot);
          } catch (const std::exception &e) {
            std::cerr << "ERROR: Initial scan failed: " << e.what()
                      << std::endl;
          }
          std::cout << "DEBUG: Initial scan finished." << std::endl;
        });

    // 2. Start FSEvents Monitor
    dispatch_queue_t queue = dispatch_queue_create("com.asimov.fsevents", NULL);

    std::string pathStr = m_watchRoot.string();
    CFStringRef mypath =
        CFStringCreateWithCString(NULL, pathStr.c_str(), kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch =
        CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);

    // Pass 'this' instance to the C-style callback
    FSEventStreamContext context = {0, (void *)this, NULL, NULL, NULL};

    FSEventStreamRef stream = FSEventStreamCreate(
        NULL, &AsimovWatcher::fsEventCallbackWrapper, &context, pathsToWatch,
        kFSEventStreamEventIdSinceNow,
        1.0, // Latency
        kFSEventStreamCreateFlagFileEvents);

    FSEventStreamSetDispatchQueue(stream, queue);

    if (!FSEventStreamStart(stream)) {
      std::cerr << "ERROR: Failed to start FSEvent stream" << std::endl;
      exit(1);
    }

    // Keep main thread alive
    dispatch_main();
  }

private:
  fs::path m_watchRoot; // Optimized: Store as fs::path
  std::vector<Rule> m_sentinels;
  std::vector<std::string> m_absoluteIgnorePrefixes;

  // --- Static Callback Wrapper ---
  static void fsEventCallbackWrapper(ConstFSEventStreamRef streamRef,
                                     void *clientCallBackInfo, size_t numEvents,
                                     void *eventPaths,
                                     const FSEventStreamEventFlags eventFlags[],
                                     const FSEventStreamEventId eventIds[]) {
    // SAFETY: Prevent C++ exceptions from unwinding into C stack frames
    try {
      AsimovWatcher *watcher = static_cast<AsimovWatcher *>(clientCallBackInfo);
      char **paths = (char **)eventPaths;
      for (size_t i = 0; i < numEvents; i++) {
        watcher->checkPath(paths[i], false, eventFlags[i]);
      }
    } catch (const std::exception &e) {
      std::cerr << "ERROR: Exception in FSEvents callback: " << e.what()
                << std::endl;
    } catch (...) {
      std::cerr << "ERROR: Unknown exception in FSEvents callback" << std::endl;
    }
  }

  // --- Core Logic ---

  void checkPath(const fs::path &path, bool parentVerified = false,
                 FSEventStreamEventFlags flags = kFSEventStreamEventFlagNone,
                 bool skipExclusionCheck = false) {
    // Optimization: Filter based on flags if available
    if (flags != kFSEventStreamEventFlagNone) {
      bool isCreated = (flags & kFSEventStreamEventFlagItemCreated);
      bool isRenamed = (flags & kFSEventStreamEventFlagItemRenamed);

      if (!isCreated && !isRenamed) {
        return;
      }

      // Special handling for Rename: FSEvents reports both source and dest.
      // Source does not exist, Destination does.
      if (isRenamed && !fs::exists(path)) {
        return;
      }
    }

    // Optimizations
    if (!skipExclusionCheck) {
      if (shouldIgnore(path))
        return;

      // If parent is not verified by caller, we must check up the tree
      if (!parentVerified && isParentExcluded(path))
        return;

      // Even if parent is verified, we must check if THIS specific directory is
      // excluded
      if (parentVerified && isExcludedFast(path.c_str()))
        return;
    }

    std::string filename = path.filename().string();
    if (path.has_parent_path()) {
      fs::path parent = path.parent_path();

      // Combined Sentinel & Target Logic
      for (const auto &rule : m_sentinels) {
        // 1. Found Sentinel (e.g. package.json)? -> Check corresponding Target
        // (node_modules)
        if (filename == rule.key) {
          fs::path target = parent / rule.value;
          if (fs::exists(target))
            applyExclusion(target);
        }
        // 2. Found Target (e.g. node_modules)? -> Check corresponding Sentinel
        // (package.json)
        else if (filename == rule.value) {
          fs::path sentinel = parent / rule.key;
          if (fs::exists(sentinel))
            applyExclusion(path);
        }
      }
    }
  }

  void scanRecursive(const fs::path &basePath) {
    // Optimization: verifying the current directory (and aborting if excluded)
    // is sufficient since we recurse top-down.
    if (isExcludedFast(basePath.c_str()))
      return;
    if (shouldIgnore(basePath))
      return;

    // Check the directory itself
    checkPath(basePath, true, kFSEventStreamEventFlagNone,
              true); // Verified locally

    std::error_code ec;
    fs::directory_iterator it(basePath, ec);
    if (ec) {
      // Permission denied or other error, just skip
      return;
    }

    for (const auto &entry : it) {
      if (entry.is_directory()) {
        scanRecursive(entry.path());
      } else if (entry.is_regular_file()) {
        checkPath(entry.path(), true); // Parent (basePath) is verified
      }
    }
  }

  // --- Helpers ---

  bool isExcludedFast(const char *path) const {
    char value[1024];
    ssize_t len =
        getxattr(path, "com.apple.metadata:com_apple_backup_excludeItem", value,
                 sizeof(value), 0, 0);
    return (len > 0);
  }

  bool isParentExcluded(const fs::path &path) const {
    fs::path current = path;
    // Walk up the tree
    while (current.has_relative_path()) {
      if (isExcludedFast(current.c_str())) {
        return true;
      }
      if (current == m_watchRoot)
        break; // Optimization: Don't check above watch root
      current = current.parent_path();
    }
    return false;
  }

  bool shouldIgnore(const fs::path &path) const {
    std::string pathStr = path.string();

    // Ensure pathStr has a trailing separator for prefix matching if it's a
    // directory However, since we don't always know if 'path' is a dir, we can
    // match:
    // 1. Exact match (without trailing slash)
    // 2. Prefix match (with trailing slash in the ignore rule)

    // To match consistently, let's append a separator to pathStr for the
    // comparison
    std::string pathStrWithSep = pathStr;
    if (pathStrWithSep.back() != fs::path::preferred_separator) {
      pathStrWithSep += fs::path::preferred_separator;
    }

    for (const auto &ignorePrefix : m_absoluteIgnorePrefixes) {
      // ignorePrefix already has a trailing separator.
      // So we check if pathStrWithSep starts with ignorePrefix.
      if (pathStrWithSep.rfind(ignorePrefix, 0) == 0) {
        return true;
      }
    }
    return false;
  }

  void applyExclusion(const fs::path &path) {
    std::string pathStr = path.string();

    // 1. Create .metadata_never_index to prevent Spotlight indexing
    fs::path spotlightParamPath = path / ".metadata_never_index";
    if (!fs::exists(spotlightParamPath)) {
      std::ofstream outfile(spotlightParamPath);
      if (outfile.good()) {
        outfile.close();
        std::cout << "DEBUG: Created .metadata_never_index in " << pathStr
                  << std::endl;
      } else {
        std::cerr << "WARN: Failed to create .metadata_never_index in "
                  << pathStr << std::endl;
      }
    }

    if (isExcludedFast(pathStr.c_str()))
      return;

    pid_t pid = fork();
    if (pid == 0) {
      // Child process
      execl("/usr/bin/tmutil", "tmutil", "addexclusion", pathStr.c_str(),
            (char *)NULL);

      // SAFETY: Use _exit in child to avoid flushing parent buffers or calling
      // dtors
      _exit(1);
    } else {
      int status;
      waitpid(pid, &status, 0);
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        std::cout << "✅ Excluded: " << pathStr << std::endl;
      } else {
        std::cerr << "❌ Failed to exclude: " << pathStr << std::endl;
      }
    }
  }
};

int main(int argc, char *argv[]) {
  // Faster I/O
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(NULL);

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <directory_to_watch> [ignore_dirs...]" << std::endl;
    return 1;
  }

  std::string watchPath = argv[1];
  std::vector<std::string> ignoredDirs;

  // Default params strictly via arguments now (controlled by plist usually)
  if (argc > 2) {
    for (int i = 2; i < argc; ++i) {
      ignoredDirs.push_back(argv[i]);
    }
  }

  AsimovWatcher watcher(watchPath, ignoredDirs);
  watcher.run();

  return 0;
}
