#!/bin/bash
# Minimal Plan9 configuration addition (safe, non-destructive)

echo "Adding Plan9 configuration to existing build..."

# Add Plan9 defines to config.h if not present
if [ -f "bfd/config.h" ] && ! grep -q "HAVE_plan9_amd64_vec" bfd/config.h; then
    cat >> bfd/config.h << 'EOD'

/* Plan9 target vectors */
#define HAVE_plan9_amd64_vec 1
#define HAVE_plan9_386_vec 1
#define HAVE_plan9_arm64_vec 1
#define HAVE_plan9_arm_vec 1
#define HAVE_plan9_power_vec 1
#define HAVE_plan9_power64_vec 1
EOD
    echo "✓ Added Plan9 defines to bfd/config.h"
fi

# Add Plan9 backend to BFD build
if [ -f "bfd/Makefile" ] && ! grep -q "bfd-plan9.lo" bfd/Makefile; then
    # Add to the BFD32_BACKENDS list before the end
    sed -i '/xtensa-modules\.lo$/a\	bfd-plan9.lo \\' bfd/Makefile
    
    # Add to the BFD32_BACKENDS_CFILES list before the end
    sed -i '/xtensa-modules\.c$/a\	bfd-plan9.c \\' bfd/Makefile
    
    echo "✓ Added Plan9 backend to BFD build"
fi

# Update Makefile SELECT_VECS - be more flexible with pattern matching
if [ -f "bfd/Makefile" ]; then
    if grep -q "SELECT_VECS.*elf64.*be_vec" bfd/Makefile; then
        # Add Plan9 vectors to any existing SELECT_VECS line
        sed -i 's/\(SELECT_VECS=[^'\'']*\)\('\''[^'\'']*\'\''[^'\'']*\)/\1,\&plan9_amd64_vec,\&plan9_386_vec,\&plan9_arm64_vec,\&plan9_arm_vec,\&plan9_power_vec,\&plan9_power64_vec\2/' bfd/Makefile
        echo "✓ Added Plan9 vectors to SELECT_VECS"
    fi
    
    if grep -q "SELECT_ARCHITECTURES.*bfd_.*_arch" bfd/Makefile; then
        # Add PowerPC architectures to any existing SELECT_ARCHITECTURES line
        sed -i 's/\(SELECT_ARCHITECTURES=[^'\'']*\)\('\''[^'\'']*\'\''[^'\'']*\)/\1,\&bfd_rs6000_arch,\&bfd_powerpc_arch\2/' bfd/Makefile
        echo "✓ Added PowerPC architectures"
    fi
fi

# Add Plan9 objects to ofiles with current objects
if [ -f "bfd/ofiles" ]; then
    CURRENT_OFILES=$(cat bfd/ofiles)
    PLAN9_OBJECTS="cpu-rs6000.lo cpu-powerpc.lo bfd-plan9.lo"
    UPDATED_OFILES="$CURRENT_OFILES"
    
    for obj in $PLAN9_OBJECTS; do
        if ! echo "$CURRENT_OFILES" | grep -q "$obj"; then
            UPDATED_OFILES="$UPDATED_OFILES $obj"
            echo "✓ Added $obj to build"
        fi
    done
    
    echo "$UPDATED_OFILES" | tr -s ' ' > bfd/ofiles
fi

# Fix target matching if file exists
if [ -f "bfd/targmatch.h" ]; then
    # Look for any aarch64-plan9 entries and fix them
    if grep -q "aarch64.*plan9" bfd/targmatch.h; then
        # Replace any ELF vectors with Plan9 vectors for plan9 targets
        sed -i '/aarch64.*plan9/,+2{
            s/HAVE_aarch64_elf64_le_vec/HAVE_plan9_arm64_vec/g
            s/aarch64_elf64_le_vec/plan9_arm64_vec/g
        }' bfd/targmatch.h
        echo "✓ Fixed target matching"
    fi
fi

echo "Plan9 configuration additions complete!"
