/*
 * Copyright (c) 2022-2023, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <AK/TypeCasts.h>
#include <LibJS/Runtime/PromiseCapability.h>
#include <LibJS/Runtime/PromiseConstructor.h>
#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/Bindings/HostDefined.h>
#include <LibWeb/HTML/Scripting/ExceptionReporter.h>
#include <LibWeb/WebIDL/ExceptionOr.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::WebIDL {

// https://webidl.spec.whatwg.org/#a-new-promise
JS::NonnullGCPtr<Promise> create_promise(JS::Realm& realm)
{
    auto& vm = realm.vm();

    // 1. Let constructor be realm.[[Intrinsics]].[[%Promise%]].
    auto constructor = realm.intrinsics().promise_constructor();

    // Return ? NewPromiseCapability(constructor).
    // NOTE: When called with %Promise%, NewPromiseCapability can't throw.
    return MUST(JS::new_promise_capability(vm, constructor));
}

// https://webidl.spec.whatwg.org/#a-promise-resolved-with
JS::NonnullGCPtr<Promise> create_resolved_promise(JS::Realm& realm, JS::Value value)
{
    auto& vm = realm.vm();

    // 1. Let value be the result of converting x to an ECMAScript value.

    // 2. Let constructor be realm.[[Intrinsics]].[[%Promise%]].
    auto constructor = realm.intrinsics().promise_constructor();

    // 3. Let promiseCapability be ? NewPromiseCapability(constructor).
    // NOTE: When called with %Promise%, NewPromiseCapability can't throw.
    auto promise_capability = MUST(JS::new_promise_capability(vm, constructor));

    // 4. Perform ! Call(promiseCapability.[[Resolve]], undefined, « value »).
    MUST(JS::call(vm, *promise_capability->resolve(), JS::js_undefined(), value));

    // 5. Return promiseCapability.
    return promise_capability;
}

// https://webidl.spec.whatwg.org/#a-promise-rejected-with
JS::NonnullGCPtr<Promise> create_rejected_promise(JS::Realm& realm, JS::Value reason)
{
    auto& vm = realm.vm();

    // 1. Let constructor be realm.[[Intrinsics]].[[%Promise%]].
    auto constructor = realm.intrinsics().promise_constructor();

    // 2. Let promiseCapability be ? NewPromiseCapability(constructor).
    // NOTE: When called with %Promise%, NewPromiseCapability can't throw.
    auto promise_capability = MUST(JS::new_promise_capability(vm, constructor));

    // 3. Perform ! Call(promiseCapability.[[Reject]], undefined, « r »).
    MUST(JS::call(vm, *promise_capability->reject(), JS::js_undefined(), reason));

    // 4. Return promiseCapability.
    return promise_capability;
}

// https://webidl.spec.whatwg.org/#resolve
void resolve_promise(JS::Realm& realm, Promise const& promise, JS::Value value)
{
    auto& vm = realm.vm();

    // 1. If x is not given, then let it be the undefined value.
    // NOTE: This is done via the default argument.

    // 2. Let value be the result of converting x to an ECMAScript value.
    // 3. Perform ! Call(p.[[Resolve]], undefined, « value »).
    MUST(JS::call(vm, *promise.resolve(), JS::js_undefined(), value));
}

// https://webidl.spec.whatwg.org/#reject
void reject_promise(JS::Realm& realm, Promise const& promise, JS::Value reason)
{
    auto& vm = realm.vm();

    // 1. Perform ! Call(p.[[Reject]], undefined, « r »).
    MUST(JS::call(vm, *promise.reject(), JS::js_undefined(), reason));
}

// https://webidl.spec.whatwg.org/#dfn-perform-steps-once-promise-is-settled
JS::NonnullGCPtr<JS::Promise> react_to_promise(Promise const& promise, Optional<ReactionSteps> on_fulfilled_callback, Optional<ReactionSteps> on_rejected_callback)
{
    auto& realm = promise.promise()->shape().realm();
    auto& vm = realm.vm();

    // 1. Let onFulfilledSteps be the following steps given argument V:
    auto on_fulfilled_steps = [on_fulfilled_callback = move(on_fulfilled_callback)](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let value be the result of converting V to an IDL value of type T.
        auto value = vm.argument(0);

        // 2. If there is a set of steps to be run if the promise was fulfilled, then let result be the result of performing them, given value if T is not undefined. Otherwise, let result be value.
        auto result = on_fulfilled_callback.has_value()
            ? TRY(Bindings::throw_dom_exception_if_needed(vm, [&] { return (*on_fulfilled_callback)(value); }))
            : value;

        // 3. Return result, converted to an ECMAScript value.
        return result;
    };

    // 2. Let onFulfilled be CreateBuiltinFunction(onFulfilledSteps, « »):
    auto on_fulfilled = JS::NativeFunction::create(realm, move(on_fulfilled_steps), 1, "");

    // 3. Let onRejectedSteps be the following steps given argument R:
    auto on_rejected_steps = [&realm, on_rejected_callback = move(on_rejected_callback)](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. Let reason be the result of converting R to an IDL value of type any.
        auto reason = vm.argument(0);

        // 2. If there is a set of steps to be run if the promise was rejected, then let result be the result of performing them, given reason. Otherwise, let result be a promise rejected with reason.
        auto result = on_rejected_callback.has_value()
            ? TRY(Bindings::throw_dom_exception_if_needed(vm, [&] { return (*on_rejected_callback)(reason); }))
            : WebIDL::create_rejected_promise(realm, reason)->promise();

        // 3. Return result, converted to an ECMAScript value.
        return result;
    };

    // 4. Let onRejected be CreateBuiltinFunction(onRejectedSteps, « »):
    auto on_rejected = JS::NativeFunction::create(realm, move(on_rejected_steps), 1, "");

    // 5. Let constructor be promise.[[Promise]].[[Realm]].[[Intrinsics]].[[%Promise%]].
    auto constructor = realm.intrinsics().promise_constructor();

    // 6. Let newCapability be ? NewPromiseCapability(constructor).
    // NOTE: When called with %Promise%, NewPromiseCapability can't throw.
    auto new_capability = MUST(JS::new_promise_capability(vm, constructor));

    // 7. Return PerformPromiseThen(promise.[[Promise]], onFulfilled, onRejected, newCapability).
    auto promise_object = verify_cast<JS::Promise>(promise.promise().ptr());
    auto value = promise_object->perform_then(on_fulfilled, on_rejected, new_capability);
    return verify_cast<JS::Promise>(value.as_object());
}

// https://webidl.spec.whatwg.org/#upon-fulfillment
JS::NonnullGCPtr<JS::Promise> upon_fulfillment(Promise const& promise, ReactionSteps steps)
{
    // 1. Return the result of reacting to promise:
    return react_to_promise(promise,
        // - If promise was fulfilled with value v, then:
        [steps = move(steps)](auto value) {
            // 1. Perform steps with v.
            // NOTE: The `return` is not immediately obvious, but `steps` may be something like
            // "Return the result of ...", which we also need to do _here_.
            return steps(value);
        },
        {});
}

// https://webidl.spec.whatwg.org/#upon-rejection
JS::NonnullGCPtr<JS::Promise> upon_rejection(Promise const& promise, ReactionSteps steps)
{
    // 1. Return the result of reacting to promise:
    return react_to_promise(promise, {},
        // - If promise was rejected with reason r, then:
        [steps = move(steps)](auto reason) {
            // 1. Perform steps with r.
            // NOTE: The `return` is not immediately obvious, but `steps` may be something like
            // "Return the result of ...", which we also need to do _here_.
            return steps(reason);
        });
}

// https://webidl.spec.whatwg.org/#mark-a-promise-as-handled
void mark_promise_as_handled(Promise const& promise)
{
    // To mark as handled a Promise<T> promise, set promise.[[Promise]].[[PromiseIsHandled]] to true.
    auto promise_object = verify_cast<JS::Promise>(promise.promise().ptr());
    promise_object->set_is_handled();
}

// https://webidl.spec.whatwg.org/#wait-for-all
void wait_for_all(JS::Realm& realm, JS::MarkedVector<JS::NonnullGCPtr<Promise>> const& promises, JS::SafeFunction<void(JS::MarkedVector<JS::Value> const&)> success_steps, JS::SafeFunction<void(JS::Value)> failure_steps)
{
    // FIXME: Fix spec typo, fullfilled --> fulfilled
    // 1. Let fullfilledCount be 0.
    size_t fulfilled_count = 0;

    // 2. Let rejected be false.
    auto rejected = false;

    // 3. Let rejectionHandlerSteps be the following steps given arg:
    auto rejection_handler_steps = [&rejected, failure_steps = move(failure_steps)](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
        // 1. If rejected is true, abort these steps.
        if (rejected)
            return JS::js_undefined();

        // 2. Set rejected to true.
        rejected = true;

        // 3. Perform failureSteps given arg.
        failure_steps(vm.argument(0));

        return JS::js_undefined();
    };

    // 4. Let rejectionHandler be CreateBuiltinFunction(rejectionHandlerSteps, « »):
    auto rejection_handler = JS::NativeFunction::create(realm, move(rejection_handler_steps), 1, "");

    // 5. Let total be promises’s size.
    auto total = promises.size();

    // 6. If total is 0, then:
    if (total == 0) {
        // 1. Queue a microtask to perform successSteps given « ».
        HTML::queue_a_microtask(nullptr, [&realm, success_steps = move(success_steps)] {
            success_steps(JS::MarkedVector<JS::Value> { realm.heap() });
        });

        // 2. Return.
        return;
    }

    // 7. Let index be 0.
    auto index = 0;

    // 8. Let result be a list containing total null values.
    auto result = JS::MarkedVector<JS::Value>(realm.heap());
    result.ensure_capacity(total);
    for (size_t i = 0; i < total; ++i)
        result.unchecked_append(JS::js_null());

    // 9. For each promise of promises:
    for (auto const& promise : promises) {
        // 1. Let promiseIndex be index.
        auto promise_index = index;

        // FIXME: This should be fulfillmentHandlerSteps
        // 2. Let fulfillmentHandler be the following steps given arg:
        auto fulfillment_handler_steps = [&](JS::VM& vm) -> JS::ThrowCompletionOr<JS::Value> {
            auto arg = vm.argument(0);

            // 1. Set result[promiseIndex] to arg.
            result[promise_index] = arg;

            // 2. Set fullfilledCount to fullfilledCount + 1.
            ++fulfilled_count;

            // 3. If fullfilledCount equals total, then perform successSteps given result.
            if (fulfilled_count == total)
                success_steps(result);

            return JS::js_undefined();
        };

        // 3. Let fulfillmentHandler be CreateBuiltinFunction(fulfillmentHandler, « »):
        auto fulfillment_handler = JS::NativeFunction::create(realm, move(fulfillment_handler_steps), 1, "");

        // 4. Perform PerformPromiseThen(promise, fulfillmentHandler, rejectionHandler).
        static_cast<JS::Promise&>(*promise->promise()).perform_then(fulfillment_handler, rejection_handler, nullptr);

        // 5. Set index to index + 1.
        ++index;
    }
}

}
