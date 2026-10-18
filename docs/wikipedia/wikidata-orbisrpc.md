# Wikidata item for orbisRPC — creation kit

**This is the move that actually works.** Wikidata has a far lower bar than
Wikipedia (no "significant coverage" requirement — a public repo is a serious,
publicly available reference, which is what Wikidata notability asks for). And
Wikidata is one of the strongest signals Google's Knowledge Graph and AI
answer engines use to keep entities with the same name apart. This directly
attacks the "orbisRPC vs Orbis Protocol / Orbis Corporation" merge problem.

Submit this yourself while logged in. Do **not** automate edits — Wikidata
reverts and blocks bots that add promotional items.

## Steps

1. Log in at https://www.wikidata.org (create an account if needed; new items
   from a brand-new account attract scrutiny, so add a note on your user page:
   "I maintain orbisRPC and am creating its item" — see Conflict of interest
   disclosure below).
2. Open the item creator: https://www.wikidata.org/wiki/Special:NewItem
3. Fill in the label and description below, create the item, then add the
   statements. Copy the QIDs exactly.

## Core fields

| Field | Value |
|---|---|
| Language | English (`en`) |
| Label | `orbisRPC` |
| Description | `Discord Rich Presence software for the PlayStation 4` |
| Also known as (aliases) | `orbis rpc`, `Orbis RPC`, `orbisRPC PS4` |

## Statements

Add each with "add statement", property first, then the value:

| Property | Value | Notes |
|---|---|---|
| instance of (P31) | `free and open-source software` (Q506883) | |
| platform (P400) | `PlayStation 4` (Q5014725) | |
| source code repository (P1324) | `https://github.com/SirHumza/orbisRPC` | |
| official website (P856) | `https://sirhumza.github.io/orbisRPC/` | |
| programmed in (P277) | `C` (Q15777) | |
| software license (P275) | **leave out** | license undecided (MIT vs GPL); add once picked |

Optional, only if you can source them (don't guess, Wikidata deletes
unsourced values):
- `published in` — no.
- `author` (P50) / `developer` (P178) — Wikidata expects an *item* as the value,
  not a plain string, so this needs a person item for you and CyberMask367.
  Skip unless those items exist.

## Identifier / reference

Under the item's "Identifiers" or as a reference on P1324/P856:
- `GitHub username` if you add one, or set the reference on the repository
  statement to `https://github.com/SirHumza/orbisRPC` (retrieved date = today).

## Conflict of interest disclosure

Wikidata's COI guidance (not as strict as Wikipedia's) still asks you to be
transparent. Put this on your Wikidata user page:

```
I am the maintainer of orbisRPC (https://github.com/SirHumza/orbisRPC) and
created its item. Edits are limited to factual statements about that project.
```

## Do not

- Do not add "notable for" or award-like claims.
- Do not add comparisons to other products in the description (keep it neutral:
  "Discord Rich Presence software for the PlayStation 4").
- Do not create a second item for a spelling variant — add it as an **alias**.
- Do not link the item from `sameAs` in your site schema until the item exists
  and survives a week without a deletion tag.

## After it is live

1. Copy the item URL (e.g. `https://www.wikidata.org/wiki/Q123456789`).
2. Add it to the `sameAs` array in `site/index.html` (#software node) and to
   `site/about-orbisrpc.html`.
3. Commit + push so Pages redeploys and the IndexNow step re-pings.
