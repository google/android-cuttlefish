#include <git2.h>
#include <git2/errors.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

int handle_git2_error(int code) {
  if (code >= 0) {
    return code;
  }
  std::cerr << "error code " << code << "\n";
  auto error = git_error_last();
  if (error != nullptr) {
    std::cerr << "error klass \"" << error->klass << "\"\n";
    std::cerr << "error \"" << error->message << "\"\n";
  }
  std::abort();
}

struct FreeGitType {
  void operator()(git_commit* commit) { git_commit_free(commit); }
  void operator()(git_diff* commit) { git_diff_free(commit); }
  void operator()(git_diff_stats* stats) { git_diff_stats_free(stats); }
  void operator()(git_index* index) { git_index_free(index); }
  void operator()(git_object* obj) { git_object_free(obj); }
  void operator()(git_reference* ref) { git_reference_free(ref); }
  void operator()(git_repository* repo) { git_repository_free(repo); }
  void operator()(git_revwalk* walk) { git_revwalk_free(walk); }
  void operator()(git_tree* tree) { git_tree_free(tree); }
  void operator()(git_tree_entry* tree) { git_tree_entry_free(tree); }
};

template<typename T>
using ManagedGitType = std::unique_ptr<T, FreeGitType>;

template<typename T, typename F, typename... Args>
ManagedGitType<T> ManagedGitErr(F function, Args... args) {
  T* output = nullptr;
  handle_git2_error(function(&output, args...));
  return ManagedGitType<T>(output);
}

struct GitBuf {
  GitBuf() : buffer({0}) {}
  GitBuf(GitBuf&& other) {
    buffer = std::move(other.buffer);
    other.buffer = {};
  }
  ~GitBuf() { git_buf_dispose(&buffer); }
  GitBuf& operator=(GitBuf&& other) {
    git_buf_dispose(&buffer);
    buffer = std::move(other.buffer);
    other.buffer = {0};
    return *this;
  }

  git_buf buffer;
};

struct GitSignature {
  GitSignature(const git_signature* signature) {
    if (signature) {
      if (signature->name) { name = signature->name; }
      if (signature->email) { email = signature->email; }
      when = signature->when;
    }
  }

  std::string name;
  std::string email;
  git_time when;

  operator git_signature() {
    return git_signature {
      name.data(),
      email.data(),
      when,
    };
  }
};

struct UnanchoredCommit {
  GitSignature author;
  GitSignature committer;
  std::string message_encoding;
  std::string message;
  std::unordered_map<std::string, GitBuf> updated_file_contents;
};

std::optional<UnanchoredCommit> FilterCommit(const git_commit& commit, const std::vector<std::pair<std::string_view, std::string_view>>& mappings) {
  auto commit_tree = ManagedGitErr<git_tree>(git_commit_tree, &commit);

  std::unordered_map<std::string, GitBuf> updated_file_contents;
  for (size_t i = 0; i < git_commit_parentcount(&commit); i++) {
    auto parent = ManagedGitErr<git_commit>(git_commit_parent, &commit, i);
    auto parent_tree = ManagedGitErr<git_tree>(git_commit_tree, parent.get());
    auto repo = git_commit_owner(&commit);
    auto diff = ManagedGitErr<git_diff>(git_diff_tree_to_tree, repo, parent_tree.get(), commit_tree.get(), nullptr);
    for (size_t i = 0; i < git_diff_num_deltas(diff.get()); i++) {
      auto delta = git_diff_get_delta(diff.get(), i);
      std::string old_file = delta->old_file.path;
      std::string new_file = delta->new_file.path;
      for (const auto& [mapping_key, mapping_value] : mappings) {
        if (mapping_key == old_file || mapping_key == new_file) {
          git_tree_entry* entry = nullptr;
          auto ret = git_tree_entry_bypath(&entry, commit_tree.get(), new_file.c_str());
          if (ret == GIT_ENOTFOUND) {
            //updated_file_contents[std::string(mapping_value)] = GitBuf();
            continue;
          } else {
            handle_git2_error(ret);
          }
          ManagedGitType<git_tree_entry> managed_entry(entry);
          auto object = ManagedGitErr<git_object>(git_tree_entry_to_object, git_commit_owner(&commit), entry);
          if (git_object_type(object.get()) != GIT_OBJECT_BLOB) {
            std::cerr << "object was not blob\n";
            std::abort();
          }
          git_blob* blob = reinterpret_cast<git_blob*>(object.get());
          GitBuf buf;
          git_buf_set(&buf.buffer, git_blob_rawcontent(blob), git_blob_rawsize(blob));
          updated_file_contents[std::string(mapping_value)] = std::move(buf);
        }
      }
    }
  }
  if (updated_file_contents.empty()) {
    return std::nullopt;
  }
  UnanchoredCommit ret = {
    GitSignature(git_commit_author(&commit)),
    GitSignature(git_commit_committer(&commit)),
  };
  if (auto encoding = git_commit_message_encoding(&commit); encoding != nullptr) {
    ret.message_encoding = encoding;
  };
  if (auto message = git_commit_message(&commit); message != nullptr) {
    ret.message = message;
  }
  ret.updated_file_contents = std::move(updated_file_contents);
  return ret;
}

std::optional<UnanchoredCommit> FixupCommit(git_repository& repo, const std::vector<std::pair<std::string_view, std::string_view>>& mappings) {
  struct Payload {
    git_repository* repo;
    std::unordered_map<std::string, GitBuf> file_contents;
    const std::vector<std::pair<std::string_view, std::string_view>>* mappings;
  };
  Payload payload;
  payload.repo = &repo;
  payload.mappings = &mappings;
  auto obj = ManagedGitErr<git_object>(git_revparse_single, &repo, "HEAD^{tree}");
  auto tree = reinterpret_cast<git_tree*>(obj.get());
  handle_git2_error(git_tree_walk(tree, GIT_TREEWALK_PRE, [](const char* root, const git_tree_entry* entry, void* void_payload) {
    Payload* payload = reinterpret_cast<Payload*>(void_payload);
    auto name = std::string(root) + std::string(git_tree_entry_name(entry));
    for (const auto& [mapping_key, mapping_value] : *(payload->mappings)) {
      if (name == mapping_key) {
        std::cerr << "Ensuring '" << name << "' is correct\n";
        auto object = ManagedGitErr<git_object>(git_tree_entry_to_object, payload->repo, entry);
        if (git_object_type(object.get()) != GIT_OBJECT_BLOB) {
          std::cerr << "object was not blob\n";
          std::abort();
        }
        git_blob* blob = reinterpret_cast<git_blob*>(object.get());
        GitBuf buf;
        git_buf_set(&buf.buffer, git_blob_rawcontent(blob), git_blob_rawsize(blob));
        (payload->file_contents)[std::string(mapping_value)] = std::move(buf);
        break;
      }
    }
    return 0;
  }, &payload));
  GitSignature signature(nullptr);
  signature.name = "No one";
  signature.email = "No-one@google.com";
  signature.when.time = 0;
  signature.when.offset = 0;
  signature.when.sign = 0;
  UnanchoredCommit ret = { signature, signature };
  ret.updated_file_contents = std::move(payload.file_contents);
  return ret;
}

void ApplyCommit(git_repository& repo, UnanchoredCommit& commit) {
  auto head = ManagedGitErr<git_object>(git_revparse_single, &repo, "HEAD");
  if (git_object_type(head.get()) != GIT_OBJECT_COMMIT) {
    std::cerr << "HEAD was not a commit\n";
    std::abort();
  }
  const git_commit* head_commit = reinterpret_cast<git_commit*>(head.get());
  auto head_tree = ManagedGitErr<git_tree>(git_commit_tree, head_commit);
  std::vector<git_tree_update> tree_updates;
  for (const auto& [path, contents] : commit.updated_file_contents) {
    git_oid blob_id;
    handle_git2_error(git_blob_create_from_buffer(&blob_id, &repo, contents.buffer.ptr, contents.buffer.size));
    tree_updates.emplace_back(git_tree_update {
      GIT_TREE_UPDATE_UPSERT,
      blob_id,
      GIT_FILEMODE_BLOB,
      path.c_str(),
    });
  }
  git_oid tree_id;
  handle_git2_error(
      git_tree_create_updated(
          &tree_id,
          &repo,
          head_tree.get(),
          tree_updates.size(),
          tree_updates.data()));
  auto tree_obj = ManagedGitErr<git_object>(git_object_lookup, &repo, &tree_id, GIT_OBJECT_TREE);
  git_tree* tree = reinterpret_cast<git_tree*>(tree_obj.get());

  auto diff = ManagedGitErr<git_diff>(git_diff_tree_to_tree, &repo, head_tree.get(), tree, nullptr);
  auto stats = ManagedGitErr<git_diff_stats>(git_diff_get_stats, diff.get());
  if (git_diff_stats_insertions(stats.get()) == 0 && git_diff_stats_deletions(stats.get()) == 0) {
    return;
  }

  git_oid new_commit_id;
  git_signature author = commit.author;
  git_signature committer = commit.committer;
  handle_git2_error(
      git_commit_create(
          &new_commit_id,
          &repo,
          "HEAD",
          &author,
          &committer,
          commit.message_encoding.empty() ? nullptr : commit.message_encoding.c_str(),
          commit.message.c_str(),
          tree,
          1,
          &head_commit));

  std::cerr << "Applied " << commit.author.email << " " << commit.message.substr(0, commit.message.find("\n")) << "\n";
}

static constexpr char kUsage[] = R"raw(
"mini merge" tool.

Creates commits in a destination repository matching commits in the source repository
filtered down to a smaller set of files.

`--help`: Print this message
`--source_repo=/path/to/git/repo`: Where to pull commits from
`--dest_repo=/path/to/git/repo`: Where to push commits to
`--map=/source/path:/dest/path`: Relative path mapping within the repository
)raw";

std::optional<std::string_view> ArgValue(std::string_view name, std::string_view arg) {
  if (arg.substr(0, name.size()) != name) {
    return std::nullopt;
  }
  arg.remove_prefix(name.size());
  return arg;
}

int RunMiniMerge(int argc, char** argv) {
  argc--;
  argv++;
  std::vector<std::string> args(argc);
  std::transform(argv, argv + argc, args.begin(), [](auto arg) { return std::string_view(arg); });

  for (size_t i = 0; i < args.size(); i++) {
    if (args[i].size() > 0 && args[i][0] == '@') {
      std::ifstream arg_file(std::string(args[i].substr(1)));
      if (!arg_file.is_open()) {
        std::cerr << "Failed to open \"" << args[i] << "\n";
        std::abort();
      }
      args.erase(args.begin() + i);
      for (std::string line; std::getline(arg_file, line);) {
        if (line.size() > 0 && line[0] != '#') {
          args.insert(args.begin() + i, line);
        }
      }
    }
  }
  for (const auto& arg : args) {
    std::cerr << "Argument \"" << arg << "\"\n";
  }

  if (std::find(args.begin(), args.end(), "--help") != args.end()) {
    std::cerr << kUsage;
  }

  std::string source_path;
  std::string destination_path;
  std::string revision_range;
  std::vector<std::pair<std::string_view, std::string_view>> mappings;
  for (const auto& arg : args) {
    if (auto path = ArgValue("--destination=", arg)) { destination_path = *path; }
    else if (auto path = ArgValue("--source=", arg)) { source_path = *path; }
    else if (auto path = ArgValue("--rev_range=", arg)) { revision_range = *path; }
    else if (auto mapping = ArgValue("--map=", arg)) {
      auto separator_index = mapping->find(":");
      if (separator_index == std::string_view::npos) {
        std::cerr << "Error in arg `--map=" << *mapping << "`: no separator\n";
        std::abort();
      }
      mappings.emplace_back(mapping->substr(0, separator_index), mapping->substr(separator_index + 1));
    }
  }
  auto source = ManagedGitErr<git_repository>(git_repository_open, source_path.c_str());
  auto dest = ManagedGitErr<git_repository>(git_repository_open, destination_path.c_str());

  std::vector<std::string_view> input_files;
  for (const auto& mapping : mappings) {
    input_files.emplace_back(mapping.first);
  }
  auto walk = ManagedGitErr<git_revwalk>(git_revwalk_new, source.get());
  handle_git2_error(git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL));
  if (revision_range.empty()) {
    handle_git2_error(git_revwalk_push_head(walk.get()));
  } else {
    handle_git2_error(git_revwalk_push_range(walk.get(), revision_range.c_str()));
  }
  git_oid oid;
  std::vector<UnanchoredCommit> commits;
  while (!git_revwalk_next(&oid, walk.get())) {
    auto commit = ManagedGitErr<git_commit>(git_commit_lookup, source.get(), &oid);
    auto filtered_commit = FilterCommit(*commit, mappings);
    if (!filtered_commit) {
      continue;
    }
    commits.emplace_back(std::move(*filtered_commit));
    std::string summary = git_commit_summary(commit.get());
    std::cerr << "Discovered " << git_commit_author(commit.get())->email << " " << summary << "\n";
  }
  for (auto it = commits.rbegin(); it != commits.rend(); it++) {
    ApplyCommit(*dest, *it);
  }

  auto fixup = FixupCommit(*source, mappings);
  if (fixup.has_value()) {
    ApplyCommit(*dest, *fixup);
  }

  auto tree_obj = ManagedGitErr<git_object>(git_revparse_single, dest.get(), "HEAD^{tree}");
  auto tree = reinterpret_cast<git_tree*>(tree_obj.get());

  auto index = ManagedGitErr<git_index>(git_repository_index, dest.get());
  handle_git2_error(git_index_read_tree(index.get(), tree));
  handle_git2_error(git_index_write(index.get()));

  git_checkout_options checkout_options;
  handle_git2_error(git_checkout_options_init(&checkout_options, GIT_CHECKOUT_OPTIONS_VERSION));
  checkout_options.checkout_strategy = GIT_CHECKOUT_FORCE;
  handle_git2_error(git_checkout_tree(dest.get(), tree_obj.get(), &checkout_options));

  return 0;
}

int main(int argc, char** argv) {
  handle_git2_error(git_libgit2_init());
  int ret = RunMiniMerge(argc, argv);
  handle_git2_error(git_libgit2_shutdown());
  return ret;
}

