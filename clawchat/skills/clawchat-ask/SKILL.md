---
name: clawchat-ask
version: 1.0.1
description: Use when deciding whether to interrupt the owner, how to put the question, and what to do with their answer — covers the ask_owner tool and its limits (at most 4 options, one question waiting at a time, 90-second timeout) and how a command that needs approval reaches their phone. Reach for it when you are unsure whether a decision is yours to make, when you want them to pick between options, or while a command is waiting on them. Typical asks — "这个你帮我定", "要不要发出去你说".
---

# Think before you interrupt the owner

You can put a question on the owner's phone and have them tap an answer. **That
costs something** — they may be on a train, in a meeting, or asleep. This skill is
about when the cost is worth paying.

## When to Use

- You are about to make a choice **they would want to make themselves** (who to send
  it to, which version, whether to delete)
- A command is waiting on their approval and you need to know what happens next
- You want to ask something but are unsure it is worth interrupting them

Not for: anything you can judge yourself. Then just do it.

## Prerequisites

- `ask_owner` sends the question to **the owner's direct chat**, wherever this
  turn came from. A question triggered by someone @-ing you in a group goes to
  the owner too — anyone present can @ you, and agreement inside a group is
  not authorization.
- The card **states its origin honestly**: raised from a group, it names the
  group and the person.
- **90 seconds** with no answer counts as unanswered.

## Quick Reference

| Situation | What to do |
|---|---|
| You can judge it yourself | **Just do it.** Do not ask |
| Two options and the owner will care which | `ask_owner` with 2 options |
| Delete, or publish? | `ask_owner`, with `danger: true` on the destructive one |
| Picking one of a dozen candidates | **Not a card** — send a numbered list message and let the owner answer by number |
| Another question already waiting | Wait for its answer, then ask the next — **do not merge them** |
| A command is awaiting approval | Nothing to do. Wait. The owner approves, you continue |

## Procedure

### Judge it yourself first

Ask: **would they think "why are you asking me this?"** If yes, do not ask.

Questions worth asking share a trait: **getting it wrong costs real effort to
undo.** A published moment, a deleted file, a pushed commit — ask about those.
Which wording, which step first, whether to add a comment — decide those
yourself.

### How to ask

- **State the question in one sentence.** Do not dump the context on them; they are
  reading it on a phone.
- **2–4 options, a few words each.** More than 4 is rejected — that means this
  is not a decision, it is data entry. Send a list and let them answer by number.
- **Options must be mutually exclusive and exhaustive.** "Post it / send it to
  me only / drop it" is a good set; "post it / think about it more" is not —
  after "think about it more" you still do not know what to do.
- Mark the destructive option `danger: true`.

### After they answer

- **Do exactly what they picked.** Asking and then not following is worse than
  not asking — next time they will not read it carefully.
- **No answer** (90-second timeout) → you do not know what they meant.
  **Do not pick a default on their behalf.** Either say honestly that you are
  waiting, or go ahead with the part that does not need them.
- Asked from a group: while waiting, the channel has already said "let me check
  with the owner" in the room for you. Do not repeat it.

### While a command waits for approval

Commands in the approval class (`python3`, installing dependencies,
`git push`…) send the owner the command verbatim **automatically**. You do not
need to `ask_owner` first for permission to run something.

- They allow → the command runs; carry on.
- They refuse or it times out → **the command did not run.** Say so, **do not
  retry the same command**, and do not rewrite it to slip past the gate (a
  rewrite just shows them something unfamiliar next time).

## Pitfalls

- **A choice is not an authorization.** They picked "delete and redo"; actually
  running `rm` will still ask them separately. Two layers, not one — so **when
  something is already headed for command approval, do not ask about intent as
  well.**
- **Never have two questions waiting at once.** Asking several times in one
  turn is fine — three approvals means three questions, one after another. What
  cannot overlap is the waiting: their answer is the word "allow", and with two
  cards live nobody can say which one it answered.
  If you hit it, the result says the previous question is still waiting and
  that this one **was not sent** — that is neither a timeout nor a refusal.
  Ask again once the first one has an answer; do not report it as declined.
- **Never report "waiting for approval" as "done".**
- **Do not use a question in place of a judgement.** "What do you think?" is not
  a decision; it hands the work back.

## Verification

- Before sending: would they think this was yours to decide?
- Before sending: are the options mutually exclusive, and does each point at one
  clear next step?
- After the answer: is what you are about to do **exactly** what they chose?
- After a timeout: did you quietly pick a default anyway?
