# Project 6 — Event Loop (CodeCrafters-style)

Build a message-passing system: actors that own state, a stream that transforms
events, and async combinators. Each stage adds a piece and ends with a
**Verify** check.

**Modules:** `concurrent.hpp`, `stream.hpp`, `adt.hpp`, `variant`.
**Compile:** `g++ -std=c++20 -pthread -I src -o app app.cpp && ./app`

---

## Stage 1 — An actor owns its state

Goal: `Actor<Msg, State>` wraps a state and a pure handler.

```cpp
#include <fp/concurrent.hpp>
#include <iostream>

int main() {
    fp::Actor<int, int> counter(0, [](int state, int msg) { return state + msg; });
    for (int i = 0; i < 10; ++i) counter.Send(1);
    while (counter.snapshot() < 10) std::this_thread::yield();
    std::cout << counter.snapshot() << "\n";   // 10
}
```

**Verify:** prints `10`.

**Concept — state as value.** The handler is a *pure* function
`(state, msg) -> state'`. The actor serializes messages through it, so the state
is only ever touched by one thread at a time.

## Stage 2 — Ask for the result

Goal: request/response via `Ask` (returns a future of the new state).

```cpp
auto fut = counter.Ask(5);
std::cout << fut.get() << "\n";   // 5
```

**Verify:** prints `5`.

**Concept — `Ask` = a query.** Unlike `Send` (fire-and-forget), `Ask` gives you
the state *after* the message is applied, as a value.

## Stage 3 — Multiple message types (a bank account)

Goal: model messages as a `variant`, `match` in the handler.

```cpp
#include <fp/all.hpp>
#include <variant>

struct Deposit  { int amount; };
struct Withdraw { int amount; };
using Message = std::variant<Deposit, Withdraw>;
using Account = fp::Actor<Message, int>;

int main() {
    Account account(0, [](int balance, Message m) {
        return fp::match(m,
            fp::case_<Deposit> ([&](auto d) { return balance + d.amount; }),
            fp::case_<Withdraw>([&](auto w) { return balance - w.amount; }));
    });
    account.Send(Deposit{100});
    std::cout << account.Ask(Withdraw{30}).get() << "\n";   // 70
}
```

**Verify:** prints `70`.

**Concept — sum types as messages.** The message *type* is a `variant`; the
handler `match`es. Add a message type → add a `case_` arm, and the compiler
checks you did.

## Stage 4 — Overdraft protection

Goal: reject a withdrawal that would go negative (clamp or reject).

```cpp
auto handler = [](int balance, Message m) {
    return fp::match(m,
        fp::case_<Deposit> ([&](auto d) { return balance + d.amount; }),
        fp::case_<Withdraw>([&](auto w) { return w.amount > balance ? balance : balance - w.amount; }));
};
```

**Verify:** `Ask(Withdraw{200})` on a balance of 100 stays `100` (rejected).

**Concept — invariants in the handler.** The handler is the single choke point
where the state changes, so the invariant ("never negative") lives in exactly
one place.

## Stage 5 — Two actors (a logger)

Goal: the account notifies a `Logger` actor on every transaction.

```cpp
using Log = fp::Actor<std::string, std::vector<std::string>>;
Log logger({}, [](std::vector<std::string> log, std::string line) {
    log.push_back(std::move(line)); return log;
});

Account account(0, [&](int balance, Message m) {
    int next = /* ... deposit/withdraw logic ... */;
    logger.Send("balance is now " + std::to_string(next));   // side effect, at the edge
    return next;
});
```

**Verify:** after a deposit, the logger's snapshot holds the message.

**Concept — explicit effects.** The handler stays *effectively* pure; the only
shared state it touches is the logger's mailbox (itself an actor). Side effects
are pushed to the edges.

## Stage 6 — A stream of events

Goal: `Stream<T>` transforms a sequence lazily.

```cpp
#include <fp/stream.hpp>

std::vector<Message> events = { Deposit{100}, Withdraw{30}, Deposit{50} };
fp::Stream<Message> s([&events, i = 0]() mutable -> std::optional<Message> {
    return i < (int)events.size() ? std::optional<Message>(events[i++]) : std::nullopt;
});

int balance = 0;
// `|` maps over the stream: `match` is lifted into every event
auto deltas = fp::out(fp::into(s) | [](Message m) {
    return fp::match(m,
        fp::case_<Deposit> ([](auto d) { return  d.amount; }),
        fp::case_<Withdraw>([](auto w) { return -w.amount; }));
});
deltas.subscribe([&](int delta) { balance += delta; });

std::cout << balance << "\n";   // 120
```

**Verify:** prints `120`.

**Concept — stream = lazy pipeline.** `into(stream) | f` maps `f` over the items
and returns a new `Stream` (nothing runs); `subscribe` runs it. Same
`map`/`filter` vocabulary as collections — now reachable with `|` — over a
sequence that arrives over time.

## Stage 7 — Filter the stream

Goal: apply a predicate before the fold.

```cpp
// `|` maps; `filter` still filters (then fold with subscribe)
auto deposits = fp::out(fp::into(s) | [](Message m) {
    return fp::match(m,
        fp::case_<Deposit> ([](auto d) { return  d.amount; }),
        fp::case_<Withdraw>([](auto w) { return -w.amount; }));
}).filter([](int delta) { return delta > 0; });   // only deposits

deposits.subscribe([&](int delta) { balance += delta; });
```

**Verify:** with `{Deposit{100}, Withdraw{30}, Deposit{50}}`, balance is `150`
(deposits only).

**Concept — compose stages.** `map` + `filter` + `subscribe` is the collection
pipeline shape, replayed over a stream.

## Stage 8 — `race` and `timeout`

Goal: asynchronous combinators over `std::future<Result<T>>`.

```cpp
using namespace std::chrono_literals;

auto slow = std::async(std::launch::async, [] { std::this_thread::sleep_for(50ms); return fp::ok(1); });
auto fast = std::async(std::launch::async, [] { return fp::ok(2); });

std::vector<fp::AsyncResult<int>> futs;
futs.push_back(std::move(slow));
futs.push_back(std::move(fast));

std::cout << fp::race(std::move(futs)).get().value() << "\n";   // 2 (first to finish)
```

**Verify:** prints `2`.

**Concept — first result wins.** `race` fans out the futures and resolves on the
first completion.

## Stage 9 — `retry`

Goal: re-run a failing operation until it succeeds or runs out of attempts.

```cpp
int attempts = 0;
auto r = fp::retry<int>([&] {
    ++attempts;
    return std::async(std::launch::async, [&] {
        return attempts >= 3 ? fp::ok(42) : fp::err<int>("again");
    });
}, 5, 1ms).get();

std::cout << r.value() << "\n";   // 42
```

**Verify:** prints `42` (and `attempts == 3`).

**Concept — error-aware async.** `retry`/`timeout`/`race` all carry `Result`, so
failure is a value even across threads.

## Stage 10 — A tiny event bus

Goal: tie it together — events from a stream, dispatched through an actor,
logged by a logger.

```cpp
// events -> stream -> transform -> actor.Apply
// (compose stages 3, 6, 5)
```

**Verify:** a sequence of deposits/withdrawals ends with the correct balance and
a log of every transition.

**Concept — layering.** Streams *transform* events; actors *own* state; loggers
*observe*. Each is a small, pure-ish piece, composed.

---

## 🏆 Extensions

1. **A `Balance` query** — a message that asks for the current state (decide how
   a *query* differs from a *command*).
2. **Transaction idempotency** — drop duplicate messages (give each a `txid`,
   remember seen ids in the state).
3. **A second consumer** — one stream, two subscribers (or two actors fed from
   the same channel).
4. **Backpressure** — use a bounded `Channel` and handle the "full" case.
5. **`Async` continuations** — `a.then(...).and_then(...)` to chain async steps.

The goal: a correct, composable event system where state only changes through
messages and the logic is pure `match`/`map`/`filter` — no shared mutable state.
