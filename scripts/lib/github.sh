# GitHub helpers for make publish. Source after repos.conf.
# The token comes from git's credential helper (on WSL: Windows Git Credential Manager,
# `git config credential.helper "/mnt/c/Program\ Files/Git/mingw64/bin/git-credential-manager.exe"`).
# It is never stored or printed.

gh_token() {
  printf 'protocol=https\nhost=github.com\n\n' | GCM_INTERACTIVE=never git credential fill 2>/dev/null | sed -n 's/^password=//p'
}

gh_api() {   # gh_api METHOD PATH [JSON] → body; exit status 0 for 2xx
  local method=$1 path=$2 data=${3:-} token out code
  token=$(gh_token)
  [ -n "$token" ] || { echo "no GitHub credential (see scripts/lib/github.sh)" >&2; return 1; }
  out=$(curl -s -w '\n%{http_code}' -X "$method" -H "Authorization: Bearer $token" \
        -H "Accept: application/vnd.github+json" ${data:+-d "$data"} "https://api.github.com$path")
  code=${out##*$'\n'}; echo "${out%$'\n'*}"
  [ "${code:0:1}" = 2 ]
}

gh_repo_exists() { gh_api GET "/repos/$GITHUB_ORG/$1" >/dev/null 2>&1; }

gh_repo_create() {   # gh_repo_create NAME "description"
  gh_repo_exists "$1" && return 0
  local private=false; [ "$VISIBILITY" = private ] && private=true
  gh_api POST "/orgs/$GITHUB_ORG/repos" \
    "$(python3 -c 'import json,sys; print(json.dumps({"name":sys.argv[1],"description":sys.argv[2],"private":sys.argv[3]=="true","has_wiki":False}))' "$1" "$2" "$private")" >/dev/null \
    && echo "created github.com/$GITHUB_ORG/$1 ($VISIBILITY)"
}

gh_url() { echo "https://github.com/$GITHUB_ORG/$1.git"; }
