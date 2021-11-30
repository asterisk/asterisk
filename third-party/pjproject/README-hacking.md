# Hacking on PJProject

## Intro
There are times when you need to troubleshoot issues with bundled pjproject
or add new features that need to be pushed upstream but...

* The source directory created by extracting the pjproject tarball is not
scanned for code changes so you have to keep forcing rebuilds.
* The source directory isn't a git repo so you can't easily create patches,
do git bisects, etc.
* Accidentally doing a make distclean will ruin your day by wiping out the
source directory, and your changes.
* etc.

Well No More!

You can now replace the `source` directory that's normally created
by the Makefile extracting the tarball, with a symlink to a "real" pjproject
git clone.  The Makefile will now detect that `source` is a real pjproject
repo and enable some advanced behaviors (and disable others).

## Setup

Let's assume you have an Asterisk development environment like so:

```plain
~/dev/asterisk/
  asterisk/
    .git/
    addons/
    ...
    third-party/
      jansson/
      pjproject/
```

### Cloning pjproject

Start by cloning a pjproject repository next to your asterisk repository.
The source of the clone depends on whether you anticipate pushing changes
back upstream or not.  If you already have a good pjproject repository clone,
read this section anyway but you probably won't have to do anything.

* For pushing upstream: (Community Contributors)
    * Make sure you have the proper ssh keys added to your github account
    so you can push changes.
    * Navigate to https://github.com/pjsip/pjproject
    * Click the "Fork" button to create a fork under your own username.

Back on your own machine...

```plain
$ cd ~/dev/asterisk
$ git clone git@github.com:<yourusername>/pjproject
```

* For pushing upstream: (Asterisk Core Team Developers)
Asterisk Core Team Developers should clone the fork we have in our own
Asterisk github organization.

```plain
$ cd ~/dev/asterisk
$ git clone git@github.com:asterisk/pjproject
```

Regardless of how you got your repo, you'll need to create an "upstream"
remote that points to the original pjproject repo.

```plain
$ cd pjproject
$ git remote add upstream https://github.com/pjsip/pjproject
```

If you're just troubleshooting and don't plan on pushing changes upstream,
you can just clone directly from the upstream pjproject repo.

```plain
$ cd ~/dev/asterisk
$ git clone https://github.com/pjsip/pjproject
```

Your directory structure should now look something like:

```plain
~/dev/asterisk/
  asterisk/
    .git/
    addons/
    ...
    third-party/
      jansson/
      pjproject/
  pjproject/
    .git
    pjlib/
    ...
```

### Adjusting Asterisk
Start with a "distcleaned" asterisk work tree then in the
asterisk/third-party/pjproject directory, create a symlink to the pjproject
clone you just created.

```plain
$ cd ~/dev/asterisk/asterisk/
$ make distclean
$ cd third-party/pjproject
$ ln -s ../../../pjproject source
```
The "source" directory is now a relative symlink to your pjproject
clone so your directory structure should now look something like:

```plain
~/dev/asterisk/
  asterisk/
    .git/
    addons/
    ...
    third-party/
      jansson/
      pjproject/
        source -> ../../../pjproject
  pjproject/
    .git
    pjlib/
    ...
```

### Adjust pjproject git ignores.
One final step is required to keep your pjproject repo from being dirtied
by the build process.  Add the following lines to your pjproject (not asterisk)
repo's .git/info/exclude file...

```plain
**/*.astdep
**/*asterisk_malloc_debug*
**/_pjsua.o
**/_pjsua.so
```
Don't add these to the top-level .gitignore file!  If you do, they'll become
part of any change you submit upstream.

## Usage

Just run `./configure` and `make` as you would for any other asterisk build.
When you make changes to pjproject source files, they'll be automatically
recompiled the next time you build asterisk.

You can do git operations in the pjproject repo while it's still symlinked
into the asterisk source.  Assuming you made the proper changes to
pjproject's .git/info/exclude file, a commit in the pjproject repo _should_ contain
only the changes you made.

You can run `make` commands directly in third-party/pjproject  The only
requirement is that an asterisk top-level `configure` had to have been
run at least once.

You can always revert to standard bundled pjproject by running an asterisk
top-level `make distclean`, removing the third-party/pjproject/source
symlink, and re-running a top-level `configure`.  That will download and
extract the pjproject tarball to the `third-party/pjproject/source`
directory as usual.

### Notes

While your pjproject repo is symlinked into the asterisk source tree,
you should not run `configure` directly in the pjproject repo.  You won't get
the proper options applied to be compatible with Asterisk.  You can run
`make` though.

Although asterisk_malloc_debug and site_config.h are applied to the pjproject
repo, No patches from the `third-party/pjproject/patches` directory are
applied.  Since you're probably working off the pjproject master branch,
the patches aren't needed.  Also, applying the patches would contaminate
the pjproject repo and you wouldn't be able to do a clean commit there.

You'll see compile and/or link warnings you wouldn't see with a normal
bundled build.


## How it works

When an asterisk top-level `configure` is run, `third-party/pjproject/configure.m4 `
checks whether `third-party/pjproject/source` is a symlink or is a git
repository.  If neither are true, the build isn't considered "out-of-tree"
and the normal pjproject bundled process occurs.
If either is true, it sets `PJPROJECT_BUNDLED_OOT=yes` for the Makefiles.

When a `make` is done, either from top-level asterisk or from the
third-party/pjproject directory, it checks `PJPROJECT_BUNDLED_OOT`
and if set to yes it...

    * Alters the behavior of `clean` and `distclean` to just run
    pjproject's `clean` or `distclean` targets and to NOT remove the
    `source` directory or symlink as it would normally do.

    * Generates `astdep` dependency files in the pjproject source tree
    if they don't already exist.  These are git-ignored by the edit
    to pjproject's `.git/info/exclude` done above.  You'll
    see new progress messages during the make as the astdep files are
    built.

    * Copies asterisk_malloc_debug.c, asterisk_malloc_debug.h and
    config_site.h from the patches directory into the pjproject source
    tree.  These are also git-ignored by the edit to pjproject's
    `.git/info/exclude` file.

    * Compiles only the out-of-date source files into their respective
    libpj libraries.  That in turn triggers the asterisk top-level
    make to re-link main/libasteriskpj.so.



