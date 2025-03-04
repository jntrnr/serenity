/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2020, Linus Groh <mail@linusgroh.de>
 * Copyright (c) 2020, Marcin Gasperowicz <xnooga@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/Function.h>
#include <AK/HashTable.h>
#include <AK/ScopeGuard.h>
#include <AK/StringBuilder.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/ArrayIterator.h>
#include <LibJS/Runtime/ArrayPrototype.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/Function.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/ObjectPrototype.h>
#include <LibJS/Runtime/Value.h>

namespace JS {

static HashTable<Object*> s_array_join_seen_objects;

ArrayPrototype::ArrayPrototype(GlobalObject& global_object)
    : Object(*global_object.object_prototype())
{
}

void ArrayPrototype::initialize(GlobalObject& global_object)
{
    auto& vm = this->vm();
    Object::initialize(global_object);
    u8 attr = Attribute::Writable | Attribute::Configurable;

    define_native_function(vm.names.filter, filter, 1, attr);
    define_native_function(vm.names.forEach, for_each, 1, attr);
    define_native_function(vm.names.map, map, 1, attr);
    define_native_function(vm.names.pop, pop, 0, attr);
    define_native_function(vm.names.push, push, 1, attr);
    define_native_function(vm.names.shift, shift, 0, attr);
    define_native_function(vm.names.toString, to_string, 0, attr);
    define_native_function(vm.names.toLocaleString, to_locale_string, 0, attr);
    define_native_function(vm.names.unshift, unshift, 1, attr);
    define_native_function(vm.names.join, join, 1, attr);
    define_native_function(vm.names.concat, concat, 1, attr);
    define_native_function(vm.names.slice, slice, 2, attr);
    define_native_function(vm.names.indexOf, index_of, 1, attr);
    define_native_function(vm.names.reduce, reduce, 1, attr);
    define_native_function(vm.names.reduceRight, reduce_right, 1, attr);
    define_native_function(vm.names.reverse, reverse, 0, attr);
    define_native_function(vm.names.sort, sort, 1, attr);
    define_native_function(vm.names.lastIndexOf, last_index_of, 1, attr);
    define_native_function(vm.names.includes, includes, 1, attr);
    define_native_function(vm.names.find, find, 1, attr);
    define_native_function(vm.names.findIndex, find_index, 1, attr);
    define_native_function(vm.names.some, some, 1, attr);
    define_native_function(vm.names.every, every, 1, attr);
    define_native_function(vm.names.splice, splice, 2, attr);
    define_native_function(vm.names.fill, fill, 1, attr);
    define_native_function(vm.names.values, values, 0, attr);
    define_property(vm.names.length, Value(0), Attribute::Configurable);

    // Use define_property here instead of define_native_function so that
    // Object.is(Array.prototype[Symbol.iterator], Array.prototype.values)
    // evaluates to true
    define_property(vm.well_known_symbol_iterator(), get(vm.names.values), attr);
}

ArrayPrototype::~ArrayPrototype()
{
}

static Function* callback_from_args(GlobalObject& global_object, const String& name)
{
    auto& vm = global_object.vm();
    if (vm.argument_count() < 1) {
        vm.throw_exception<TypeError>(global_object, ErrorType::ArrayPrototypeOneArg, name);
        return nullptr;
    }
    auto callback = vm.argument(0);
    if (!callback.is_function()) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotAFunction, callback.to_string_without_side_effects());
        return nullptr;
    }
    return &callback.as_function();
}

static void for_each_item(VM& vm, GlobalObject& global_object, const String& name, AK::Function<IterationDecision(size_t index, Value value, Value callback_result)> callback, bool skip_empty = true)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return;

    auto initial_length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return;

    auto* callback_function = callback_from_args(global_object, name);
    if (!callback_function)
        return;

    auto this_value = vm.argument(1);

    for (size_t i = 0; i < initial_length; ++i) {
        auto value = this_object->get(i);
        if (vm.exception())
            return;
        if (value.is_empty()) {
            if (skip_empty)
                continue;
            value = js_undefined();
        }

        auto callback_result = vm.call(*callback_function, this_value, value, Value((i32)i), this_object);
        if (vm.exception())
            return;

        if (callback(i, value, callback_result) == IterationDecision::Break)
            break;
    }
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::filter)
{
    auto* new_array = Array::create(global_object);
    for_each_item(vm, global_object, "filter", [&](auto, auto value, auto callback_result) {
        if (callback_result.to_boolean())
            new_array->indexed_properties().append(value);
        return IterationDecision::Continue;
    });
    return Value(new_array);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::for_each)
{
    for_each_item(vm, global_object, "forEach", [](auto, auto, auto) {
        return IterationDecision::Continue;
    });
    return js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::map)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    auto initial_length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    auto* new_array = Array::create(global_object);
    new_array->indexed_properties().set_array_like_size(initial_length);
    for_each_item(vm, global_object, "map", [&](auto index, auto, auto callback_result) {
        if (vm.exception())
            return IterationDecision::Break;
        new_array->define_property(index, callback_result);
        return IterationDecision::Continue;
    });
    return Value(new_array);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::push)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    if (this_object->is_array()) {
        auto* array = static_cast<Array*>(this_object);
        for (size_t i = 0; i < vm.argument_count(); ++i)
            array->indexed_properties().append(vm.argument(i));
        return Value(static_cast<i32>(array->indexed_properties().array_like_size()));
    }
    auto length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    auto argument_count = vm.argument_count();
    auto new_length = length + argument_count;
    if (new_length > MAX_ARRAY_LIKE_INDEX) {
        vm.throw_exception<TypeError>(global_object, ErrorType::ArrayMaxSize);
        return {};
    }
    for (size_t i = 0; i < argument_count; ++i) {
        this_object->put(length + i, vm.argument(i));
        if (vm.exception())
            return {};
    }
    auto new_length_value = Value((i32)new_length);
    this_object->put(vm.names.length, new_length_value);
    if (vm.exception())
        return {};
    return new_length_value;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::unshift)
{
    auto* array = Array::typed_this(vm, global_object);
    if (!array)
        return {};
    for (size_t i = 0; i < vm.argument_count(); ++i)
        array->indexed_properties().insert(i, vm.argument(i));
    return Value(static_cast<i32>(array->indexed_properties().array_like_size()));
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::pop)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    if (this_object->is_array()) {
        auto* array = static_cast<Array*>(this_object);
        if (array->indexed_properties().is_empty())
            return js_undefined();
        return array->indexed_properties().take_last(array).value.value_or(js_undefined());
    }
    auto length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    if (length == 0) {
        this_object->put(vm.names.length, Value(0));
        return js_undefined();
    }
    auto index = length - 1;
    auto element = this_object->get(index).value_or(js_undefined());
    if (vm.exception())
        return {};
    this_object->delete_property(index);
    if (vm.exception())
        return {};
    this_object->put(vm.names.length, Value((i32)index));
    if (vm.exception())
        return {};
    return element;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::shift)
{
    auto* array = Array::typed_this(vm, global_object);
    if (!array)
        return {};
    if (array->indexed_properties().is_empty())
        return js_undefined();
    auto result = array->indexed_properties().take_first(array);
    if (vm.exception())
        return {};
    return result.value.value_or(js_undefined());
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::to_string)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    auto join_function = this_object->get(vm.names.join);
    if (vm.exception())
        return {};
    if (!join_function.is_function())
        return ObjectPrototype::to_string(vm, global_object);
    return vm.call(join_function.as_function(), this_object);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::to_locale_string)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    if (s_array_join_seen_objects.contains(this_object))
        return js_string(vm, "");
    s_array_join_seen_objects.set(this_object);
    ArmedScopeGuard unsee_object_guard = [&] {
        s_array_join_seen_objects.remove(this_object);
    };

    auto length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};

    String separator = ","; // NOTE: This is implementation-specific.
    StringBuilder builder;
    for (size_t i = 0; i < length; ++i) {
        if (i > 0)
            builder.append(separator);
        auto value = this_object->get(i).value_or(js_undefined());
        if (vm.exception())
            return {};
        if (value.is_nullish())
            continue;
        auto* value_object = value.to_object(global_object);
        if (!value_object)
            return {};
        auto locale_string_result = value_object->invoke("toLocaleString");
        if (vm.exception())
            return {};
        auto string = locale_string_result.to_string(global_object);
        if (vm.exception())
            return {};
        builder.append(string);
    }
    return js_string(vm, builder.to_string());
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::join)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    // This is not part of the spec, but all major engines do some kind of circular reference checks.
    // FWIW: engine262, a "100% spec compliant" ECMA-262 impl, aborts with "too much recursion".
    // Same applies to Array.prototype.toLocaleString().
    if (s_array_join_seen_objects.contains(this_object))
        return js_string(vm, "");
    s_array_join_seen_objects.set(this_object);
    ArmedScopeGuard unsee_object_guard = [&] {
        s_array_join_seen_objects.remove(this_object);
    };

    auto length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    String separator = ",";
    if (!vm.argument(0).is_undefined()) {
        separator = vm.argument(0).to_string(global_object);
        if (vm.exception())
            return {};
    }
    StringBuilder builder;
    for (size_t i = 0; i < length; ++i) {
        if (i > 0)
            builder.append(separator);
        auto value = this_object->get(i).value_or(js_undefined());
        if (vm.exception())
            return {};
        if (value.is_nullish())
            continue;
        auto string = value.to_string(global_object);
        if (vm.exception())
            return {};
        builder.append(string);
    }

    return js_string(vm, builder.to_string());
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::concat)
{
    auto* array = Array::typed_this(vm, global_object);
    if (!array)
        return {};

    auto* new_array = Array::create(global_object);
    new_array->indexed_properties().append_all(array, array->indexed_properties());
    if (vm.exception())
        return {};

    for (size_t i = 0; i < vm.argument_count(); ++i) {
        auto argument = vm.argument(i);
        if (argument.is_array()) {
            auto& argument_object = argument.as_object();
            new_array->indexed_properties().append_all(&argument_object, argument_object.indexed_properties());
            if (vm.exception())
                return {};
        } else {
            new_array->indexed_properties().append(argument);
        }
    }

    return Value(new_array);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::slice)
{
    auto* array = Array::typed_this(vm, global_object);
    if (!array)
        return {};

    auto* new_array = Array::create(global_object);
    if (vm.argument_count() == 0) {
        new_array->indexed_properties().append_all(array, array->indexed_properties());
        if (vm.exception())
            return {};
        return new_array;
    }

    ssize_t array_size = static_cast<ssize_t>(array->indexed_properties().array_like_size());
    auto start_slice = vm.argument(0).to_i32(global_object);
    if (vm.exception())
        return {};
    auto end_slice = array_size;

    if (start_slice > array_size)
        return new_array;

    if (start_slice < 0)
        start_slice = end_slice + start_slice;

    if (vm.argument_count() >= 2) {
        end_slice = vm.argument(1).to_i32(global_object);
        if (vm.exception())
            return {};
        if (end_slice < 0)
            end_slice = array_size + end_slice;
        else if (end_slice > array_size)
            end_slice = array_size;
    }

    for (ssize_t i = start_slice; i < end_slice; ++i) {
        new_array->indexed_properties().append(array->get(i));
        if (vm.exception())
            return {};
    }

    return new_array;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::index_of)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    i32 length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    if (length == 0)
        return Value(-1);
    i32 from_index = 0;
    if (vm.argument_count() >= 2) {
        from_index = vm.argument(1).to_i32(global_object);
        if (vm.exception())
            return {};
        if (from_index >= length)
            return Value(-1);
        if (from_index < 0)
            from_index = max(length + from_index, 0);
    }
    auto search_element = vm.argument(0);
    for (i32 i = from_index; i < length; ++i) {
        auto element = this_object->get(i);
        if (vm.exception())
            return {};
        if (strict_eq(element, search_element))
            return Value(i);
    }
    return Value(-1);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::reduce)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    auto initial_length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};

    auto* callback_function = callback_from_args(global_object, "reduce");
    if (!callback_function)
        return {};

    size_t start = 0;

    auto accumulator = js_undefined();
    if (vm.argument_count() > 1) {
        accumulator = vm.argument(1);
    } else {
        bool start_found = false;
        while (!start_found && start < initial_length) {
            auto value = this_object->get(start);
            if (vm.exception())
                return {};
            start_found = !value.is_empty();
            if (start_found)
                accumulator = value;
            start += 1;
        }
        if (!start_found) {
            vm.throw_exception<TypeError>(global_object, ErrorType::ReduceNoInitial);
            return {};
        }
    }

    auto this_value = js_undefined();

    for (size_t i = start; i < initial_length; ++i) {
        auto value = this_object->get(i);
        if (vm.exception())
            return {};
        if (value.is_empty())
            continue;

        accumulator = vm.call(*callback_function, this_value, accumulator, value, Value((i32)i), this_object);
        if (vm.exception())
            return {};
    }

    return accumulator;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::reduce_right)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    auto initial_length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};

    auto* callback_function = callback_from_args(global_object, "reduceRight");
    if (!callback_function)
        return {};

    int start = initial_length - 1;

    auto accumulator = js_undefined();
    if (vm.argument_count() > 1) {
        accumulator = vm.argument(1);
    } else {
        bool start_found = false;
        while (!start_found && start >= 0) {
            auto value = this_object->get(start);
            if (vm.exception())
                return {};
            start_found = !value.is_empty();
            if (start_found)
                accumulator = value;
            start -= 1;
        }
        if (!start_found) {
            vm.throw_exception<TypeError>(global_object, ErrorType::ReduceNoInitial);
            return {};
        }
    }

    auto this_value = js_undefined();

    for (int i = start; i >= 0; --i) {
        auto value = this_object->get(i);
        if (vm.exception())
            return {};
        if (value.is_empty())
            continue;

        accumulator = vm.call(*callback_function, this_value, accumulator, value, Value((i32)i), this_object);
        if (vm.exception())
            return {};
    }

    return accumulator;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::reverse)
{
    auto* array = Array::typed_this(vm, global_object);
    if (!array)
        return {};

    if (array->indexed_properties().is_empty())
        return array;

    MarkedValueList array_reverse(vm.heap());
    auto size = array->indexed_properties().array_like_size();
    array_reverse.ensure_capacity(size);

    for (ssize_t i = size - 1; i >= 0; --i) {
        array_reverse.append(array->get(i));
        if (vm.exception())
            return {};
    }

    array->set_indexed_property_elements(move(array_reverse));

    return array;
}

static void array_merge_sort(VM& vm, GlobalObject& global_object, Function* compare_func, MarkedValueList& arr_to_sort)
{
    // FIXME: it would probably be better to switch to insertion sort for small arrays for
    // better performance
    if (arr_to_sort.size() <= 1)
        return;

    MarkedValueList left(vm.heap());
    MarkedValueList right(vm.heap());

    left.ensure_capacity(arr_to_sort.size() / 2);
    right.ensure_capacity(arr_to_sort.size() / 2 + (arr_to_sort.size() & 1));

    for (size_t i = 0; i < arr_to_sort.size(); ++i) {
        if (i < arr_to_sort.size() / 2) {
            left.append(arr_to_sort[i]);
        } else {
            right.append(arr_to_sort[i]);
        }
    }

    array_merge_sort(vm, global_object, compare_func, left);
    if (vm.exception())
        return;
    array_merge_sort(vm, global_object, compare_func, right);
    if (vm.exception())
        return;

    arr_to_sort.clear();

    size_t left_index = 0, right_index = 0;

    while (left_index < left.size() && right_index < right.size()) {
        auto x = left[left_index];
        auto y = right[right_index];

        double comparison_result;

        if (x.is_undefined() && y.is_undefined()) {
            comparison_result = 0;
        } else if (x.is_undefined()) {
            comparison_result = 1;
        } else if (y.is_undefined()) {
            comparison_result = -1;
        } else if (compare_func) {
            auto call_result = vm.call(*compare_func, js_undefined(), left[left_index], right[right_index]);
            if (vm.exception())
                return;

            if (call_result.is_nan()) {
                comparison_result = 0;
            } else {
                comparison_result = call_result.to_double(global_object);
                if (vm.exception())
                    return;
            }
        } else {
            // FIXME: It would probably be much better to be smarter about this and implement
            // the Abstract Relational Comparison in line once iterating over code points, rather
            // than calling it twice after creating two primitive strings.

            auto x_string = x.to_primitive_string(global_object);
            if (vm.exception())
                return;
            auto y_string = y.to_primitive_string(global_object);
            if (vm.exception())
                return;

            auto x_string_value = Value(x_string);
            auto y_string_value = Value(y_string);

            // Because they are called with primitive strings, these abstract_relation calls
            // should never result in a VM exception.
            auto x_lt_y_relation = abstract_relation(global_object, true, x_string_value, y_string_value);
            ASSERT(x_lt_y_relation != TriState::Unknown);
            auto y_lt_x_relation = abstract_relation(global_object, true, y_string_value, x_string_value);
            ASSERT(y_lt_x_relation != TriState::Unknown);

            if (x_lt_y_relation == TriState::True) {
                comparison_result = -1;
            } else if (y_lt_x_relation == TriState::True) {
                comparison_result = 1;
            } else {
                comparison_result = 0;
            }
        }

        if (comparison_result <= 0) {
            arr_to_sort.append(left[left_index]);
            left_index++;
        } else {
            arr_to_sort.append(right[right_index]);
            right_index++;
        }
    }

    while (left_index < left.size()) {
        arr_to_sort.append(left[left_index]);
        left_index++;
    }

    while (right_index < right.size()) {
        arr_to_sort.append(right[right_index]);
        right_index++;
    }
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::sort)
{
    auto* array = vm.this_value(global_object).to_object(global_object);
    if (vm.exception())
        return {};

    auto callback = vm.argument(0);
    if (!callback.is_undefined() && !callback.is_function()) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotAFunction, callback.to_string_without_side_effects());
        return {};
    }

    auto original_length = length_of_array_like(global_object, *array);
    if (vm.exception())
        return {};

    MarkedValueList values_to_sort(vm.heap());

    for (size_t i = 0; i < original_length; ++i) {
        auto element_val = array->get(i);
        if (vm.exception())
            return {};

        if (!element_val.is_empty())
            values_to_sort.append(element_val);
    }

    // Perform sorting by merge sort. This isn't as efficient compared to quick sort, but
    // quicksort can't be used in all cases because the spec requires Array.prototype.sort()
    // to be stable. FIXME: when initially scanning through the array, maintain a flag
    // for if an unstable sort would be indistinguishable from a stable sort (such as just
    // just strings or numbers), and in that case use quick sort instead for better performance.
    array_merge_sort(vm, global_object, callback.is_undefined() ? nullptr : &callback.as_function(), values_to_sort);
    if (vm.exception())
        return {};

    for (size_t i = 0; i < values_to_sort.size(); ++i) {
        array->put(i, values_to_sort[i]);
        if (vm.exception())
            return {};
    }

    // The empty parts of the array are always sorted to the end, regardless of the
    // compare function. FIXME: For performance, a similar process could be used
    // for undefined, which are sorted to right before the empty values.
    for (size_t i = values_to_sort.size(); i < original_length; ++i) {
        array->delete_property(i);
        if (vm.exception())
            return {};
    }

    return array;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::last_index_of)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    i32 length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    if (length == 0)
        return Value(-1);
    i32 from_index = length - 1;
    if (vm.argument_count() >= 2) {
        from_index = vm.argument(1).to_i32(global_object);
        if (vm.exception())
            return {};
        if (from_index >= 0)
            from_index = min(from_index, length - 1);
        else
            from_index = length + from_index;
    }
    auto search_element = vm.argument(0);
    for (i32 i = from_index; i >= 0; --i) {
        auto element = this_object->get(i);
        if (vm.exception())
            return {};
        if (strict_eq(element, search_element))
            return Value(i);
    }
    return Value(-1);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::includes)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    i32 length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};
    if (length == 0)
        return Value(false);
    i32 from_index = 0;
    if (vm.argument_count() >= 2) {
        from_index = vm.argument(1).to_i32(global_object);
        if (vm.exception())
            return {};
        if (from_index >= length)
            return Value(false);
        if (from_index < 0)
            from_index = max(length + from_index, 0);
    }
    auto value_to_find = vm.argument(0);
    for (i32 i = from_index; i < length; ++i) {
        auto element = this_object->get(i).value_or(js_undefined());
        if (vm.exception())
            return {};
        if (same_value_zero(element, value_to_find))
            return Value(true);
    }
    return Value(false);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::find)
{
    auto result = js_undefined();
    for_each_item(
        vm, global_object, "find", [&](auto, auto value, auto callback_result) {
            if (callback_result.to_boolean()) {
                result = value;
                return IterationDecision::Break;
            }
            return IterationDecision::Continue;
        },
        false);
    return result;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::find_index)
{
    auto result_index = -1;
    for_each_item(
        vm, global_object, "findIndex", [&](auto index, auto, auto callback_result) {
            if (callback_result.to_boolean()) {
                result_index = index;
                return IterationDecision::Break;
            }
            return IterationDecision::Continue;
        },
        false);
    return Value(result_index);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::some)
{
    auto result = false;
    for_each_item(vm, global_object, "some", [&](auto, auto, auto callback_result) {
        if (callback_result.to_boolean()) {
            result = true;
            return IterationDecision::Break;
        }
        return IterationDecision::Continue;
    });
    return Value(result);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::every)
{
    auto result = true;
    for_each_item(vm, global_object, "every", [&](auto, auto, auto callback_result) {
        if (!callback_result.to_boolean()) {
            result = false;
            return IterationDecision::Break;
        }
        return IterationDecision::Continue;
    });
    return Value(result);
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::splice)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    auto initial_length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};

    auto relative_start = vm.argument(0).to_i32(global_object);
    if (vm.exception())
        return {};

    size_t actual_start;

    if (relative_start < 0)
        actual_start = max((ssize_t)initial_length + relative_start, (ssize_t)0);
    else
        actual_start = min((size_t)relative_start, initial_length);

    size_t insert_count = 0;
    size_t actual_delete_count = 0;

    if (vm.argument_count() == 1) {
        actual_delete_count = initial_length - actual_start;
    } else if (vm.argument_count() >= 2) {
        insert_count = vm.argument_count() - 2;
        i32 delete_count = vm.argument(1).to_i32(global_object);
        if (vm.exception())
            return {};

        actual_delete_count = min((size_t)max(delete_count, 0), initial_length - actual_start);
    }

    size_t new_length = initial_length + insert_count - actual_delete_count;

    if (new_length > MAX_ARRAY_LIKE_INDEX) {
        vm.throw_exception<TypeError>(global_object, ErrorType::ArrayMaxSize);
        return {};
    }

    auto removed_elements = Array::create(global_object);

    for (size_t i = 0; i < actual_delete_count; ++i) {
        auto value = this_object->get(actual_start + i);
        if (vm.exception())
            return {};

        removed_elements->indexed_properties().append(value);
    }

    if (insert_count < actual_delete_count) {
        for (size_t i = actual_start; i < initial_length - actual_delete_count; ++i) {
            auto from = this_object->get(i + actual_delete_count);
            if (vm.exception())
                return {};

            auto to = i + insert_count;

            if (!from.is_empty()) {
                this_object->put(to, from);
            } else {
                this_object->delete_property(to);
            }
            if (vm.exception())
                return {};
        }

        for (size_t i = initial_length; i > new_length; --i) {
            this_object->delete_property(i - 1);
            if (vm.exception())
                return {};
        }
    } else if (insert_count > actual_delete_count) {
        for (size_t i = initial_length - actual_delete_count; i > actual_start; --i) {
            auto from = this_object->get(i + actual_delete_count - 1);
            if (vm.exception())
                return {};

            auto to = i + insert_count - 1;

            if (!from.is_empty()) {
                this_object->put(to, from);
            } else {
                this_object->delete_property(to);
            }
            if (vm.exception())
                return {};
        }
    }

    for (size_t i = 0; i < insert_count; ++i) {
        this_object->put(actual_start + i, vm.argument(i + 2));
        if (vm.exception())
            return {};
    }

    this_object->put(vm.names.length, Value((i32)new_length));
    if (vm.exception())
        return {};

    return removed_elements;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::fill)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    ssize_t length = length_of_array_like(global_object, *this_object);
    if (vm.exception())
        return {};

    ssize_t relative_start = 0;
    ssize_t relative_end = length;

    if (vm.argument_count() >= 2) {
        relative_start = vm.argument(1).to_i32(global_object);
        if (vm.exception())
            return {};
    }

    if (vm.argument_count() >= 3) {
        relative_end = vm.argument(2).to_i32(global_object);
        if (vm.exception())
            return {};
    }

    size_t from, to;

    if (relative_start < 0)
        from = max(length + relative_start, 0L);
    else
        from = min(relative_start, length);

    if (relative_end < 0)
        to = max(length + relative_end, 0L);
    else
        to = min(relative_end, length);

    for (size_t i = from; i < to; i++) {
        this_object->put(i, vm.argument(0));
        if (vm.exception())
            return {};
    }

    return this_object;
}

JS_DEFINE_NATIVE_FUNCTION(ArrayPrototype::values)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};

    return ArrayIterator::create(global_object, this_object, Object::PropertyKind::Value);
}

}
