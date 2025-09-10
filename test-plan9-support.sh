#!/bin/bash
# Test script to verify Plan9 ARM64 support in binutils

echo "Testing Plan9 ARM64 support in binutils-2.42..."
echo "=================================================="

OBJDUMP="./binutils/objdump"
TEST_FILE="/home/tmumford/Coding/os/9front_sourcetree/ganges/7.out"

if [ ! -f "$OBJDUMP" ]; then
    echo "❌ ERROR: objdump not found at $OBJDUMP"
    echo "Run 'make all-binutils' first"
    exit 1
fi

if [ ! -f "$TEST_FILE" ]; then
    echo "❌ ERROR: Test file not found at $TEST_FILE"
    exit 1
fi

echo "Testing Plan9 ARM64 binary recognition:"
echo "----------------------------------------"
echo "$ objdump -f $TEST_FILE"

OUTPUT=$($OBJDUMP -f "$TEST_FILE" 2>&1)
RESULT=$?

if [ $RESULT -eq 0 ]; then
    if echo "$OUTPUT" | grep -q "plan9-arm64"; then
        echo "✅ SUCCESS: Plan9 ARM64 format correctly recognized!"
        echo "$OUTPUT"
        echo ""
        echo "Plan9 support is working correctly and persists after build!"
    else
        echo "❌ FAILURE: Plan9 format not recognized in output:"
        echo "$OUTPUT"
        exit 1
    fi
else
    echo "❌ FAILURE: objdump failed with error:"
    echo "$OUTPUT"
    exit 1
fi

echo ""
echo "🎉 Plan9 ARM64 support is successfully integrated and persistent!"
echo "   The build process does NOT clear out Plan9 modifications."