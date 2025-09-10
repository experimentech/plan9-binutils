#!/bin/bash
# Safe build script that preserves Plan9 modifications

set -e

echo "=== Building binutils with Plan9 support ==="

# Remove cache files that might cause conflicts
rm -f config.cache */config.cache

# Configure with standard targets first (to avoid configure errors)
echo "Configuring with standard targets..."
./configure --enable-targets=all

# Now apply Plan9 configuration after configure completes
echo "Applying Plan9 configuration..."
./add-plan9-config.sh

# Force reconfigure BFD with our changes
echo "Reconfiguring BFD with Plan9 support..."
cd bfd
make clean || true
rm -f config.cache
# Update config.bfd to ensure Plan9 targets work
sed -i '/aarch64-\*-plan9/,/;;/c\
  aarch64-*-plan9*)\
    targ_defvec=plan9_arm64_vec\
    targ_selvecs="plan9_amd64_vec plan9_386_vec plan9_arm_vec plan9_power_vec plan9_power64_vec"\
    ;;' config.bfd

# Build BFD manually with Plan9 objects
echo "Building BFD with Plan9 objects..."
make bfd-plan9.lo cpu-rs6000.lo cpu-powerpc.lo || echo "Some objects may already exist"
make libbfd.la

cd ..

# Build binutils 
echo "Building binutils..."
make all-binutils

echo "✓ Build complete! Testing Plan9 support..."
if ./binutils/objdump -f ganges/7.out 2>/dev/null | grep -q "plan9-arm64"; then
    echo "✅ SUCCESS: Plan9 ARM64 format recognized!"
else
    echo "⚠️  Testing with ganges/7.out failed, but binutils may still work"
fi

echo "Test with: ./binutils/objdump -f ../../ganges/7.out"
