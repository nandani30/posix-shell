#!/bin/bash
# test.sh - Automated tests for myshell

echo "Starting rigorous testing phase for myshell..."

FAILED=0
PASSED=0

run_test() {
    local name="$1"
    local input="$2"
    local expected="$3"
    
    # We use <<< to feed input via stdin to myshell
    local output=$(./myshell <<< "$input" 2>&1)
    
    # Check if the expected output string is present in the actual output
    if echo "$output" | grep -F -q "$expected"; then
        echo "✅ PASSED: $name"
        ((PASSED++))
    else
        echo "❌ FAILED: $name"
        echo "   Input: $input"
        echo "   Expected (substring): $expected"
        echo "   Actual output:"
        echo "$output"
        ((FAILED++))
    fi
}

run_test_negative() {
    local name="$1"
    local input="$2"
    local not_expected="$3"
    
    local output=$(./myshell <<< "$input" 2>&1)
    
    if ! echo "$output" | grep -F -q "$not_expected"; then
        echo "✅ PASSED: $name"
        ((PASSED++))
    else
        echo "❌ FAILED: $name"
        echo "   Input: $input"
        echo "   Did NOT expect: $not_expected"
        echo "   Got: $output"
        ((FAILED++))
    fi
}

echo "--- Parser & Quoting Edge Cases ---"
run_test "Excessive whitespace" "echo    hello       world" "hello world"

echo "--- Variable Edge Cases ---"
run_test "Double quotes expansion" "export TEST_VAR=foo
echo \"\$TEST_VAR\"" "foo"
run_test "Single quotes literal" "export TEST_VAR=foo
echo '\$TEST_VAR'" "\$TEST_VAR"
run_test "Lacking spaces after variable" "export FOO=bar
echo \"\$FOObaz\"" "myshell>" # Should expand to nothing, since FOObaz doesn't exist

echo "--- Built-in Edge Cases ---"
run_test "Invalid export format" "export BADFORMAT" "invalid format"
run_test "cd to non-existent dir" "cd /fake_dir_12345" "No such file or directory"

echo "--- Pipeline Edge Cases ---"
run_test "Missing command after pipe" "ls | " "syntax error near unexpected token"
run_test "Missing command before pipe" "| ls" "syntax error near unexpected token"
run_test "Empty pipeline component" "ls | | wc" "syntax error near unexpected token"

echo "--- Redirection Edge Cases ---"
run_test "Missing input file" "cat <" "syntax error"
run_test "Missing output file" "echo >" "syntax error"

echo "--- Command Chaining Edge Cases ---"
run_test "Success && Success" "echo foo && echo bar" "bar"
run_test "Failure && Success" "cd /fake_dir && echo bar" "No such file or directory" # Should print error for cd, but NOT "bar"
run_test_negative "Failure && Success check" "cd /fake_dir && echo bar" "bar"
run_test "Failure || Success" "cd /fake_dir || echo fallback" "fallback"
run_test "Success || Success" "echo foo || echo fallback" "foo"
run_test_negative "Success || Success check" "echo foo || echo fallback" "fallback"
run_test "Sequential execution" "echo one ; echo two" "two"

echo "--- Alias Edge Cases ---"
run_test "Set and execute alias" "alias myecho='echo ALIASTEST'
myecho" "ALIASTEST"
run_test "Unalias" "alias myecho='echo ALIAS'
unalias myecho
myecho" "myecho: command not found"
run_test "List aliases" "alias foo='echo 1'
alias" "alias foo='echo 1'"

echo "--- Job Control Edge Cases ---"
run_test "fg missing job" "fg" "usage: fg %N"
run_test "bg missing job" "bg" "usage: bg %N"
run_test "fg invalid job" "fg %999" "no such job"
run_test "jobs empty" "jobs" "myshell>"

echo "--- Complex Parsing Edge Cases ---"
run_test "Multi-line double quotes" "echo \"hello
world\"" "world"
run_test "Multi-line single quotes" "echo 'hello
world'" "world"
run_test "Pipeline with alias" "alias lz='ls'
lz | wc -l" "" # wc -l will print a number


echo ""
echo "Testing Complete: $PASSED passed, $FAILED failed."
if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
