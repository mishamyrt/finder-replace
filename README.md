# Finder Replace

Open your preferred file manager when you click Finder in the Dock.
A small utility for macOS 13+, with no system patches or SIP changes.

Only plain left-clicks are replaced. Right-clicks and clicks with modifier keys
keep their usual behavior.

## Install and start

Install with Homebrew, choose an app, and start the background service.
For example, with [Bloom](https://bloomapp.club):

```sh
brew install mishamyrt/tap/finder-replace
defaults write co.myrt.finder-replace ApplicationPath -string "/Applications/Bloom.app"
brew services start finder-replace
```

Run without `sudo`. The service starts now and whenever you log in.
Stop any copy running in a terminal first.

## Allow Accessibility access

In **System Settings → Privacy & Security → Accessibility**, add
`finder-replace`. To find its path:

```sh
echo "$(brew --prefix)/opt/finder-replace/bin/finder-replace"
```

Use **⌘⇧G** in the file picker to paste the path. Permission granted to your
terminal may not cover the background service. After granting access, run:

```sh
brew services restart finder-replace
```

If the event tap still fails, also check **Input Monitoring**.

## Change or disable the replacement

Set `ApplicationPath` to the full path of another `.app`, then restart the service.
Paths with spaces work; `~` and `$HOME` is not expanded. Settings are read at startup.

To clear the setting:

```sh
defaults delete co.myrt.finder-replace ApplicationPath
brew services restart finder-replace
```

With no setting or an empty value, the program exits without intercepting clicks.
An invalid path also stops the program and reports an error.

## Manage the service

```sh
brew services info finder-replace       # Check status
brew services restart finder-replace    # Reload settings
brew services stop finder-replace       # Stop and disable login startup
```

The service restarts after a crash. Configuration or permission errors require
a manual restart after you fix them. Stop the service before uninstalling.

## Troubleshooting

Read the service log:

```sh
tail -f "$(brew --prefix)/var/log/finder-replace.log"
```

For detailed click diagnostics, stop the service and run in a terminal:

```sh
brew services stop finder-replace
finder-replace --version
finder-replace --debug
```

Press **Ctrl+C** to stop, then use `brew services start finder-replace` to resume
background operation. Run only one copy at a time.

For building, testing, and releases, see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](./LICENSE)
