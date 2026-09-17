# Project 6 — Event Loop

Build a small message-passing system: an **actor** that owns state and mutates
it only through messages, plus a **stream** that transforms a sequence of
events. This project introduces **state-as-value** and **message passing**.

**Modules:** `concurrent.hpp`, `stream.hpp`, `adt.hpp`, `variant`.
**Compile:** `g++ -std=c++20 -pthread -I src -o app app.cpp && ./app`

---

## Step 1 — An actor owns its state

An `Actor<Msg, State>` wraps a state and a *pure* handler `State(State, Msg)`.
You send messages; it applies them one at a time. Start with a counter.

```cpp
#include <fp/concurrent.hpp>
#include <iostream>

int main() {
    fp::Actor<int, int> counter(0, [](int state, int msg) { return state + msg; });

    for (int i = 0; i < 10; ++i) counter.Send(1);
    while (counter.snapshot() < 10) std::this_thread::yield();

    std::cout << "count = " << counter.snapshot() << "\n";   // 10
}
```

**Concept — state as value:** the handler is a *pure* function `(state, msg) ->
state'`. There's no mutation, no `this->count++`; the actor serializes messages
through the function, so the state is only ever touched by one thread at a time.
This is the "state machine as a fold over a message stream" idea.

## Step 2 — Ask for the result

`Send` is fire-and-forget. `Ask` returns a `std::future<State>` with the state
*after* that message is applied.

```cpp
#include <fp/concurrent.hpp>
#include <iostream>

int main() {
    fp::Actor<int, int> counter(0, [](int state, int msg) { return state + msg; });

    auto fut = counter.Ask(5);         // apply +5, return the new state
    std::cout << "after +5: " << fut.get() << "\n";   // 5
}
```

**Concept — request/response:** `Ask` turns the message into a query — you get
the resulting state back as a value (a future), rather than polling `snapshot()`.

## Step 3 — A bank account with multiple message types

A real actor takes *several kinds* of messages. Model them as a `std::variant`
and `match` inside the handler.

```cpp
#include <fp/all.hpp>
#include <variant>
#include <iostream>

struct Deposit  { int amount; };
struct Withdraw { int amount; };
using Message = std::variant<Deposit, Withdraw>;
using Account = fp::Actor<Message, int>;   // state = balance

int main() {
    Account account(0, [](int balance, Message m) {
        return fp::match(m,
            fp::case_<Deposit> ([&](auto d) { return balance + d.amount; }),
            fp::case_<Withdraw>([&](auto w) { return balance - w.amount; }));
    });

    account.Send(Deposit{100});
    auto fut = account.Ask(Withdraw{30});
    std::cout << "balance = " << fut.get() << "\n";   // 70
}
```

**Concept — sum types as messages:** the message *type* is a `variant`, and the
handler `match`es on it. Adding a new message type means adding a `case_` arm —
and the compiler checks you did (exhaustiveness again). This is how event
systems stay correct as they grow.

## Step 4 — A stream of events

`Stream<T>` transforms a sequence of items. Feed it events from a pull source,
`map`/`filter`, then `subscribe`.

```cpp
#include <fp/all.hpp>
#include <variant>
#include <iostream>

struct Deposit  { int amount; };
struct Withdraw { int amount; };
using Message = std::variant<Deposit, Withdraw>;

int main() {
    std::vector<Message> events = { Deposit{100}, Withdraw{30}, Deposit{50} };

    fp::Stream<Message> s([&events, i = 0]() mutable -> std::optional<Message> {
        return i < (int)events.size() ? std::optional<Message>(events[i++]) : std::nullopt;
    });

    int balance = 0;
    s.map([](Message m) {                    // deposit -> +amount, withdraw -> -amount
            return fp::match(m,
                fp::case_<Deposit> ([](auto d) { return  d.amount; }),
                fp::case_<Withdraw>([](auto w) { return -w.amount; }));
        })
     .subscribe([&](int delta) { balance += delta; });

    std::cout << "final balance = " << balance << "\n";   // 120
}
```

**Concept — stream = lazy pipeline:** `map` builds a new `Stream` (nothing runs
yet); `subscribe` runs it to completion, pulling items and applying the
transforms. The same `map`/`filter` vocabulary as collections, over a *sequence
that arrives over time*.

---

## 🏆 Challenge

Grow it into a real event system:

1. **A `Balance` query message** — add `struct Balance {};` to the `Message`
   variant. Handling a query in `Actor` is awkward because the handler returns a
   state, not an answer — use `Ask(Balance{})` and return the *balance* from the
   handler when the message is a query (the state is the balance, so it works
   out), or add a dedicated query path.
2. **Overdraft protection** — make `Withdraw` fail (leave the balance unchanged,
   or clamp at 0) when it would go negative. Decide how to *signal* the failure.
3. **Two actors** — add a second actor (e.g. a `Logger`) that the account
   notifies on every transaction (send to the logger from inside the handler,
   or from the caller).
4. **A `filter` on the stream** — only apply deposits over a threshold, or drop
   withdrawals that would overdraw.

**Hints:**
- For "notify another actor", the handler can capture a reference to the other
  actor and `Send` to it as a side effect (the handler stays *effectively* pure
  — the only shared state is the other actor's mailbox).
- `Stream::filter(pred)` keeps items where `pred` holds, lazily.
- Keep the handler a pure function; move side effects to the edges (`Send` to a
  logger is a deliberate, explicit effect).

The goal is a tiny, correct event system where state only changes through
messages and the logic is pure `match`/`map`/`filter` — no shared mutable state.
