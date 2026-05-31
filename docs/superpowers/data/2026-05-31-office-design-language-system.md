# THE OFFICE — Design Language System (DLS)

**Date:** 2026-05-31
**Source:** Distilled from two independent deep-research reports Hemanth commissioned (ChatGPT + Gemini), 2026-05-31, on best-in-class UI/UX for an AI-agent coordination room. Both converged hard; this is the synthesized, implementable token set. The driving brief: a dark, calm, classy "mission-control room," not a social chat app — its job is the human supervisor's **trust at a glance**.

This is the visual source-of-truth for THE OFFICE app (spec: `2026-05-31-the-office-app-design.md`). Resolves the gray/black/white governance conflict: the Office gets an explicit **operational-tool exception** for *semantic status colors only* — otherwise strictly monochrome, **SVG icons (`fill="currentColor"`), never emoji as UI chrome.**

## Reference apps (and what each contributes)

- **Linear** — the benchmark for calm density + dark-mode discipline. Near-black canvas, one accent, hairline borders, restrained typography, side-panel "peek" pattern (inspect without losing context).
- **Discord** — the roster rail / presence mechanics. Persistent member list, presence dots, role grouping, collapsible-on-hover sidebar, background-luminance zoning (no borders needed).
- **NASA OpenMCT** — mission-critical telemetry / the "trusted control room." Object-composable dashboards, right-hand Inspect panel. Model for the Operations + Autonomous surfaces.
- **Vercel** — dense status scanning. Branch/commit-hash/timestamp alignment with green/red/pulsing status dots = the exact template for the roster's "last commit" line.
- **Zulip** — topic-threaded async clarity. Model for Discussion-mode (each turn = its own space).

## Color tokens (dark, never pure black; never pure white text)

| Token | Value | Role |
|---|---|---|
| `--surface-base` | `#08090A` | Deepest bg — main message stream canvas |
| `--surface-raised` | `#121212` | Roster rail, nav sidebars (Z-axis proximity) |
| `--surface-overlay` | `#1D1E20` | Modals, dispatch drawers, popovers (highest elevation) |
| `--border-hairline` | `#23252A` | 1px panel/row separators — no heavy borders, no shadows |
| `--text-high` | `rgba(255,255,255,0.87)` | Primary text, agent names, input |
| `--text-med` | `rgba(255,255,255,0.60)` | Timestamps, metadata, idle text |
| `--text-low` | `rgba(255,255,255,0.38)` | Disabled, placeholders, auto activity lines |
| `--accent-primary` | `#5E6AD2` | Muted lavender-blue. The ONE accent — focus rings, active lane, human's primary actions. Functional only, never decorative. |
| `--status-error` | `#E57373` | Softened red — blockers. Never `#FF0000`. |
| `--status-active` | `#81C784` | Muted green — active processing / wakeful / pass |
| `--status-warning` | `#FFB74D` | Amber — nudge / idle / waiting |
| `--status-system` | `#8A8F98` | Cold gray — system logs, telemetry, structural |
| `--killswitch` | `#D32F2F` | Hard red — the ONLY element that defies the restrained palette (emergency stop) |

Elevation = step luminance up ~5–8% per level (shadows don't read on dark). Depth via lighter surfaces / translucent white overlays, not drop shadows.

## Typography (two faces; tabular figures for all numbers)

- **Primary:** neo-grotesque sans (Inter / SF Pro / Roboto) — high x-height, dense legibility.
- **Mono:** JetBrains Mono / Fira Code — git commits, terminal output, JSON, reasoning dumps **only**.
- **All numerals** (timestamps, hashes, latency, agent IDs): `font-variant-numeric: tabular-nums;` — Bloomberg-style vertical alignment for fast scanning.

| Token | Size (base 14) | Weight | Line-height | Tracking | Use |
|---|---|---|---|---|---|
| `--type-display` | 24px | 600 | 1.2 | -0.02em | Empty-state headers, modal titles, STOP warning |
| `--type-heading` | 16px | 600 | 1.4 | -0.01em | Panel titles, role names in inspect |
| `--type-body-bold` | 14px | 500 | 1.5 | 0.01em | Agent names, handles, primary button text |
| `--type-body` | 14px | 400 | 1.5 | 0.01em | Chat messages, instructions |
| `--type-caption` | 12px | 500 | 1.4 | 0.02em | Roster status, timestamps, badge text |
| `--type-mono` | 12px | 400 | 1.4 | 0 | Commits, code, logs, JSON |

Slight POSITIVE tracking on small text (white bleeds/closes glyph negative space on dark); NEGATIVE tracking on large display.

## Spacing, layout, motion

- **4px baseline grid** — all margin/padding/size snaps to 4/8/12/16/24/32.
- **CSS Grid shell** for the major regions (sidebar / main canvas / right inspect) — rigid, no reflow when an agent dumps a wall of text.
- **Roster rail:** 280px fixed (optionally collapse→36px on hover, 0.2s ease).
- **Message column:** max-width 800–960px, centered (readable line length on wide monitors).
- **Motion = utilitarian only:** `--transition-fast: 150ms ease-in-out` for hovers/color shifts. New messages appear instantly or rapid opacity fade. **No bouncy/spring/physics motion** (erodes operator trust). Don't slide elements that push data around. Status changes reflect *immediately*. Allowed: gentle 2s opacity pulse on a blocked dot; radial-fill ring on the kill-switch hold.

## Components

- **No chat bubbles.** Flat left-aligned text on the canvas, grouped by sender (avatar+name on first of a cluster only; subsequent = hover-timestamp in left margin).
- **Panels/cards:** `--surface-overlay` + 1px `--border-hairline`. Buttons flat/outline (accent text, not solid fills), 4–6px radius. Inputs: dark fill + hairline.
- **Badges:** small chips. `nudge`/uninterruptible = `--surface-overlay` chip + padlock SVG. `blocked` = soft-red, gentle pulse.
- **Tables/dense data:** mono + tabular-nums, alternating row luminance or hairlines.

## The 9 surfaces (mapped)

1. **Live message stream** — `--surface-base`, centered 800–960px, sender grouping. Message kinds: *normal* (high-emphasis flat text), *activity* (low-emphasis mono, dense 1.2 line-height, git/cloud SVG prefix), *system* (centered hairline rule + caption badge), *blocker* (1px `--status-error` border + `rgba(229,115,115,.05)` fill + solid BLOCKED badge).
2. **BROTHERHOOD roster rail** — `--surface-raised`, 280px. Per agent: avatar(24px)+presence dot, designation (body-bold), truncated current-task line (caption/med), Vercel-style mono commit line (hash + status icon + "2m ago"). Pulse on blocked, padlock chip on nudge.
3. **Message types** — as above (structural, not loud color).
4. **Foreman/dispatch panel** — right-hand slide-out drawer (OpenMCT Inspect pattern), keeps chat+roster visible. Vertical stepper: done (med + check) / active (high + accent spinner) / pending (low).
5. **Autonomous console** — toggling collapses chat → CSS-Grid dashboard of agent swimlanes (OpenMCT telemetry), live tabular metrics. **Kill-switch:** the one palette exception — hard-red rectangular header button, uppercase "STOP ALL AUTONOMY", **click-and-hold 1.5s + radial ring** to fire (no accidental trigger).
6. **Shared-resource (MCP) lane** — header rail / token-ring: holder's avatar linked to the resource chip by a glowing accent bracket; queued agents = translucent (0.5) avatar stack.
7. **Staging / review area** — GitHub-PR/Vercel-staging style. Tabular gate checklist; green left-edge when checks pass. Two outline buttons: "Approve & Merge" (accent), "Reject & Re-prompt" (error) — outlines, not solid blocks.
8. **Cross-engine membership** — engine origin must NOT dominate identity. 10px monochrome SVG engine glyph nested on the avatar corner (or low-opacity card watermark), `--text-low`. Same shape for all engines = equal members.
9. **Discussion-mode** — Zulip-style; remove infinite feed. Active speaker centered/enlarged in a dominant card; others queued horizontally; `--transition-fast` cycles turns. Boardroom, not chatroom.

## Anti-patterns (hard NO)

1. Consumer chat bubbles (waste width, jagged reading line, codes "casual").
2. Emoji as UI chrome (inconsistent, informal) — custom monochrome SVG `fill="currentColor"` for ALL statuses/badges/nav. (Humans may emoji in message *text*; the app chrome may not.)
3. High-contrast borders / heavy shadows — luminance shifts + 1px hairlines only.
4. Bouncy/physics motion — determinism; instant or rapid linear fade.
5. Pure black `#000` bg + pure white `#fff` text — halation/eye-strain. Elevated charcoal base, opacity-tiered text.
6. Cluttered / info-behind-deep-menus — key status visible at a glance in roster/main; "SEE and TRUST" demands it.
