# Arcade Box bridge v1

Arcade Box is optional. Arcade Wheel looks for an `arcade-box` executable on `PATH` and invokes:

```sh
arcade-box tools --json
```

The command should return within 500 ms and print either a JSON array or an object containing a `tools` array:

```json
{
  "tools": [
    {
      "id": "download-audio",
      "name": "Download Audio",
      "icon": "audio-x-generic",
      "description": "Download audio from a URL",
      "inputs": ["clipboard-url"],
      "presets": [{"id": "best-audio", "name": "Best Audio"}]
    }
  ]
}
```

A saved wheel action stores its `toolId`, optional `input`, and optional `preset`. Arcade Wheel invokes it without showing Arcade Box's main UI:

```sh
arcade-box run --tool download-audio --input clipboard-url --preset best-audio
```

Arcade Wheel starts the process and returns immediately. Arcade Box owns input collection, saved preset semantics, job progress, and failures after launch. When Arcade Box is missing, its slots stay in the configuration and are marked unavailable in Settings.
