---
name: clawchat-liveware
version: 1.0.2
description: Use when the owner wants you to build, host, update or publish a liveware — a small web app of your own that opens inside ClawChat (a game, a dashboard, a report, a tool). Covers what a liveware actually is (a web service you run on this machine, exposed through a tunnel), the hosting steps, and how to hand it over — registering it as an app tile AND sending its URL so a tappable card appears in the conversation. Typical asks — "做个骰子 liveware", "帮我做个小应用", "把这个做成能在 ClawChat 里打开的页面", "做个每日报表页面".
---

# Hosting a liveware

## What a liveware actually is

**A liveware is a web service you run on this machine.** Not a file. Not a
document. A process, listening on a local port, reached from ClawChat through a
tunnel:

```
                                                    ┌─► a card in the chat
your web server            liveware tunnel          │   (you send the URL)
127.0.0.1:<port>   ──►   app-….apps.clawling.io  ──►┤
(you run this)           (the CLI sets this up)     └─► an app tile
                                                        (register_app)
```

Four things, and **all four are required**:

1. **A running server.** You start it and keep it running. If the process dies,
   the liveware goes dark.
2. **A tunnel.** The `liveware` CLI publishes your local port at a public
   `app-….apps.clawling.io` address.
3. **A registration.** `register_app` puts that address in the owner's chat as a
   tile, reachable any time from 「…」→ the app panel.
4. **A card in the conversation.** Your finishing message carries the public URL,
   and ClawChat renders it as a tappable liveware card right there in the chat.

**The last two are separate surfaces, and you owe the owner both.** The tile is
where the liveware *lives* — it is how they find it next week. The card is how
they open it *now*, without being told where to tap. Registering but not sending
the URL is the common miss: the thing exists and the owner cannot see it.

**Writing an HTML file is step zero, not the job.** A file on your disk is
unreachable from the owner's phone — they are on a different device. If you stop
after writing one, you have not made a liveware.

## When to Use

- The owner asks for a small app / page / game / dashboard **that opens in
  ClawChat**
- You want to give a result a real interface instead of a wall of text
- You are changing or re-publishing a liveware you already host

Not for: sending a one-off picture or file (use `send_image` / `send_file`), or
ordinary local development that nobody opens from ClawChat.

## Prerequisites — check first, stop if unmet

1. **Call `liveware_login` first.** Do not run `liveware login` yourself: the
   tool holds your ClawChat credentials and hands them to the CLI without
   putting them in your context. It is idempotent, and **it installs the CLI**
   when this machine has none — so it is also the check. The first call may take
   a moment while it downloads. **Its answer names the binary's absolute path**:
   if `liveware` is not on your PATH afterwards, invoke it by that path (or put
   its directory on PATH) for every `liveware` command below — do not go
   looking for another install.
2. **Read its answer before doing anything else.** If it says this environment
   cannot host livewares, that is final: say so plainly and stop. **Do not look
   for a substitute** — see Pitfalls.

## Quick Reference

| Step | Command | Notes |
|---|---|---|
| 1 | `liveware_login()` | The tool, not the CLI. Idempotent. It installs the CLI if this machine has none, so **call it before checking for `liveware`**; its answer names the binary's absolute path — use that when `liveware` is not on your PATH |
| 2 | *start your server* | Any stack. Must listen on `127.0.0.1:<port>`. **Detach it** — see below |
| 3 | `liveware app create "<name>"` | Prints the `app-…` id. Capture it |
| 4 | `liveware tunnel bind <app id> http://127.0.0.1:<port>` | One-shot. Prints `domain <host>` — the public URL is `https://<host>` |
| 5 | `liveware agent` | **Resident daemon.** Ready when it prints `relay grpc control connected`. Detach it too |
| 6 | `register_app(name=…, app_id=…, url=…)` | **Only after the URL really loads** |
| 7 | *say you are done — with the URL in the message* | Paste `https://app-….apps.clawling.io` into your reply. That is what draws the card |

## Procedure

### Building the page

Write it wherever you normally work. Keep it self-contained — a liveware runs in
a sandboxed container, so no assumption about the host page, and no reliance on
extensions.

**Knowing who is looking.** When someone opens your liveware from ClawChat, the
tunnel adds their ClawChat user id to every request as a header — `X-User-Id`
(also sent as `X-Clawchat-User-Id`; same value). Read it **server-side only**.
Page JavaScript cannot see it, and a request that arrives without it did not come
through ClawChat — treat that viewer as anonymous and never trust a
client-supplied value for those header names.

### Serving it — and making it outlive this turn

Bind the server to `127.0.0.1` — the tunnel reaches it locally, and `0.0.0.0`
exposes it on the owner's network for no benefit. Note the port; step 4 needs the
exact number.

⚠️ **You are a short-lived process. Your server must not be.** Each turn runs in
a fresh process that exits when you finish answering. A server started in the
foreground — or as an ordinary child — **dies with it**, and so does
`liveware agent`. The liveware then goes dark minutes after you reported success.

So detach both, and redirect their output to a log you can read later:

```
nohup <your server command> > /tmp/<app>-server.log 2>&1 &
nohup liveware agent        > /tmp/<app>-agent.log  2>&1 &
```

(On Windows, start them so they survive the turn — e.g. `Start-Process -WindowStyle Hidden`.)

**Nothing restarts them for you.** A machine reboot, a ClawChat restart, or the
owner killing the process leaves a registered tile pointing at nothing. If the
owner says the liveware stopped working, **check whether your processes are still
alive before theorising** — that is the first thing to rule out, and the honest
answer is usually "it stopped, I will start it again", not a story about the
network.

### Publishing it

Run steps 3–5 in order. Each one prints something the next one needs, so read the
output rather than assuming the shape.

`tunnel-agent` is **resident**: it is what carries public requests back to your
local port. If it is not running, the public URL answers `404 upstream not
found` — the address exists, nothing is behind it.

### Registering it

Call `register_app` once you have actually loaded the public URL and seen your
page. The tile is a promise to the owner that tapping it opens something.

### Handing it over — put the URL in your message

**Then send the public URL as part of your finishing message.** Not a file path,
not "it is in the app panel" on its own — the URL itself, verbatim:

```
做好了 —— 掷一下试试:
https://app-2f9c1d….apps.clawling.io
点右上角「…」→「应用」也能随时再找到它。
```

ClawChat sees a `….apps.clawling.io` link in your text and renders it as a
**liveware card** — a tappable entry right in the conversation. This costs you
nothing extra and is the difference between the owner opening it now and the
owner reading instructions about where to tap.

⚠️ **The card shows the domain, not your app's name.** It renders as ✦ plus
`app-2f9c1d…` — an opaque id, because the card is drawn from the URL alone. So
the sentence you put around it is not decoration, it is the label: say what the
thing is and what to do with it, or the owner gets a tappable box of hex.

Three things to get right:

- **The URL must be the real one** `liveware tunnel bind` returned. A card that
  opens to nothing is worse than no card.
- **One card per message.** ClawChat draws the *first* liveware URL in the text
  and ignores the rest — so if you are handing over two livewares, send two
  messages.
- **Include the URL, do not describe it.** A bare URL and a markdown link both
  work, prose around it is fine, and a Chinese full stop right after it is safe.
  What draws no card is telling the owner *where* the liveware is instead of
  giving them the link.

Then tell them plainly: the name, what it does, and that it also lives under
「…」→ the app tile for next time.

## Pitfalls

- **Do not report a file path as a delivery.** `/Users/…/index.html` means
  nothing on the owner's phone.
- **Do not finish without the URL in the message.** "It is registered, look in
  the app panel" makes the owner go hunting for something you could have handed
  them. Registered and un-sent is a half-delivery.
- **Do not `register_app` a URL you are not serving.** A tile that opens to an
  error is worse than no tile — it looks like you finished.
- **Do not substitute another host when the CLI is missing.** Not a third-party
  tunnel, not a file-sharing link, not an image of the page. Those are not
  livewares: they skip the container that a liveware runs in, so they lose its
  guarantees, and `X-User-Id` never arrives. Say the environment cannot host,
  and leave the page on disk for when it can.
- **Do not invent a URL.** Only ever register what `liveware tunnel bind`
  actually returned.
- **A dead server is a dead liveware.** If you stop the process, say so — do not
  let the owner discover it by tapping.
- **Never explain an outage with something you did not check.** "The tunnel
  reconnected", "an MCP disconnect", "a network blip" — if you have not looked at
  whether your own two processes are running, you are inventing. Look first.

## Verification

Before telling the owner it is ready:

1. Confirm **both processes are alive** (your server and `liveware agent`) —
   they are the two that silently take everything down with them.
2. `curl` the public URL from this machine and confirm you get **your page**,
   not `404 upstream not found` and not `401 open in clawchat`.
3. Confirm `register_app` returned success.
4. Confirm the URL is **in the message you are about to send**. Not in your
   notes, not in a file you wrote — in the reply itself.

`401 open in clawchat` is **not a failure** — it means the tunnel is working and
is refusing an unauthenticated request. Livewares open inside ClawChat, so that
response is what a bare `curl` is supposed to get once things are set up
correctly; the owner tapping the tile will get the page.
