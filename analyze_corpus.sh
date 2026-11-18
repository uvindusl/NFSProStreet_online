#!/bin/bash
# Corpus dcl analysis script

DISASM="/home/uvindu/Projects/nfsprostree-recompile/lifter/tools/build/shader_disasm"
SHADER_DIR="/tmp/opencode/ps-shaders"
OUTPUT_DIR="/home/uvindu/Projects/nfsprostree-recompile/debug/abi-corpus-counts"
mkdir -p "$OUTPUT_DIR"

# Output files
VERTEX_USAGE="$OUTPUT_DIR/vertex_usage.tsv"
VERTEX_TEXCOORD="$OUTPUT_DIR/vertex_texcoord.tsv"
PIXEL_SAMPLER="$OUTPUT_DIR/pixel_sampler.tsv"
PIXEL_TEXCOORD="$OUTPUT_DIR/pixel_texcoord.tsv"
SUMMARY="$OUTPUT_DIR/summary.txt"

# Clear outputs
> "$VERTEX_USAGE"
> "$VERTEX_TEXCOORD"
> "$PIXEL_SAMPLER"
> "$PIXEL_TEXCOORD"
> "$SUMMARY"

echo "=== Shader dcl Corpus Analysis ===" | tee "$SUMMARY"
echo "Generated: $(date)" | tee -a "$SUMMARY"
echo "" | tee -a "$SUMMARY"

# Track totals
declare -A VS_VERSIONS
declare -A PS_VERSIONS

# Process vertex shaders
echo "Processing vertex shaders..." | tee -a "$SUMMARY"
for blob in /tmp/opencode/ps-shaders/vs_*.bin; do
    [ -e "$blob" ] || continue
    name=$(basename "$blob" .bin)
    version=$(echo "$name" | cut -d'_' -f1-2)  # vs_2_0, vs_3_0, vs_1_1
    VS_VERSIONS["$version"]=$((${VS_VERSIONS["$version"]:-0} + 1))
    
    # Run disassembler
    out=$("$DISASM" "$blob" 2>/dev/null)
    
    # Extract dcl lines for vertex
    echo "$out" | grep -E "^\s+[0-9]+\s+dcl\s+v[0-9]+\s+<-" | while IFS= read -r line; do
        # Format: "   0 dcl     v0   <- POSITION0"
        # Extract register and usage
        reg=$(echo "$line" | sed -E 's/.*dcl\s+(v[0-9]+)\s+<-/\1/')
        usage=$(echo "$line" | sed -E 's/.*<-\s+([A-Z0-9]+).*/\1/')
        echo -e "$version\t$reg\t$usage" >> "$VERTEX_USAGE"
        
        # Also track TEXCOORD indices
        if [[ "$usage" == TEXCOORD* ]]; then
            idx=$(echo "$usage" | sed 's/TEXCOORD//')
            echo -e "$version\t$idx" >> "/tmp/vertex_texcoord_idx.txt"
        fi
    done
done

# Process pixel shaders
echo "Processing pixel shaders..." | tee -a "$SUMMARY"
for blob in /tmp/opencode/ps-shaders/ps_*.bin; do
    [ -e "$blob" ] || continue
    name=$(basename "$blob" .bin)
    version=$(echo "$name" | cut -d'_' -f1-2)  # ps_2_0, ps_3_0
    PS_VERSIONS["$version"]=$((${PS_VERSIONS["$version"]:-0} + 1))
    
    # Run disassembler
    out=$("$DISASM" "$blob" 2>/dev/null)
    
    # Extract dcl lines for pixel
    echo "$out" | grep -E "^\s+[0-9]+\s+dcl\s+[tas][0-9]+" | while IFS= read -r line; do
        # Format: "   0 dcl     t0" or "   1 dcl     a0" or "   2 dcl     s0"
        reg=$(echo "$line" | sed -E 's/.*dcl\s+([tas][0-9]+).*/\1/')
        type=$(echo "$reg" | cut -c1)
        idx=$(echo "$reg" | cut -c2-)
        echo -e "$version\t$type\t$idx" >> "$PIXEL_SAMPLER"
    done
    
    # Also capture texture coordinate registers (dcl t#)
    echo "$out" | grep -E "^\s+[0-9]+\s+dcl\s+t[0-9]+" | while IFS= read -r line; do
        reg=$(echo "$line" | sed -E 's/.*dcl\s+(t[0-9]+).*/\1/')
        idx=$(echo "$reg" | cut -c2-)
        echo -e "$version\t$idx" >> "/tmp/pixel_texcoord_idx.txt"
    done
done

# Wait for background processes
wait

# Generate histograms
echo "" | tee -a "$SUMMARY"
echo "=== Vertex Shader Versions ===" | tee -a "$SUMMARY"
for v in "${!VS_VERSIONS[@]}"; do
    echo "  $v: ${VS_VERSIONS[$v]} blobs" | tee -a "$SUMMARY"
done

echo "" | tee -a "$SUMMARY"
echo "=== Pixel Shader Versions ===" | tee -a "$SUMMARY"
for v in "${!PS_VERSIONS[@]}"; do
    echo "  $v: ${PS_VERSIONS[$v]} blobs" | tee -a "$SUMMARY"
done

# Vertex USAGE histogram
echo "" | tee -a "$SUMMARY"
echo "=== Vertex Input Usage Histogram ===" | tee -a "$SUMMARY"
if [ -f "$VERTEX_USAGE" ]; then
    awk -F'\t' '{print $2}' "$VERTEX_USAGE" | sort | uniq -c | sort -rn | while read count usage; do
        echo "  $usage: $count" | tee -a "$SUMMARY"
    done
fi

# Vertex TEXCOORD index histogram
echo "" | tee -a "$SUMMARY"
echo "=== Vertex TEXCOORD Index Histogram ===" | tee -a "$SUMMARY"
if [ -f "/tmp/vertex_texcoord_idx.txt" ]; then
    awk -F'\t' '{print $2}' "/tmp/vertex_texcoord_idx.txt" | sort -n | uniq -c | while read count idx; do
        echo "  TEXCOORD$idx: $count" | tee -a "$SUMMARY"
    done
fi

# Pixel Sampler histogram
echo "" | tee -a "$SUMMARY"
echo "=== Pixel Sampler (s#) Histogram ===" | tee -a "$SUMMARY"
if [ -f "$PIXEL_SAMPLER" ]; then
    awk -F'\t' '$2=="s" {print $3}' "$PIXEL_SAMPLER" | sort -n | uniq -c | while read count idx; do
        echo "  s$idx: $count" | tee -a "$SUMMARY"
    done
fi

# Pixel Texture Register (t#) histogram
echo "" | tee -a "$SUMMARY"
echo "=== Pixel Texture Register (t#) Histogram ===" | tee -a "$SUMMARY"
if [ -f "/tmp/pixel_texcoord_idx.txt" ]; then
    awk -F'\t' '{print $2}' "/tmp/pixel_texcoord_idx.txt" | sort -n | uniq -c | while read count idx; do
        echo "  t$idx: $count" | tee -a "$SUMMARY"
    done
fi

# Pixel Attribute (a#) histogram
echo "" | tee -a "$SUMMARY"
echo "=== Pixel Attribute (a#) Histogram ===" | tee -a "$SUMMARY"
if [ -f "$PIXEL_SAMPLER" ]; then
    awk -F'\t' '$2=="a" {print $3}' "$PIXEL_SAMPLER" | sort -n | uniq -c | while read count idx; do
        echo "  a$idx: $count" | tee -a "$SUMMARY"
    done
fi

# Verify against design doc claims
echo "" | tee -a "$SUMMARY"
echo "=== Design Doc Verification ===" | tee -a "$SUMMARY"
echo "Design doc claims:" | tee -a "$SUMMARY"
echo "  dcl s: s0=189,s1=93,s2=70,s3=48,s4=26,s5=12,s6=6" | tee -a "$SUMMARY"
echo "  dcl a: a0=183,a1=80,a2=13,a3=12,a4=6,a5=3,a6=3,a7=1" | tee -a "$SUMMARY"
echo "" | tee -a "$SUMMARY"
echo "Measured:" | tee -a "$SUMMARY"

# Compare sampler
if [ -f "$PIXEL_SAMPLER" ]; then
    for i in {0..6}; do
        count=$(awk -F'\t' -v idx="$i" '$2=="s" && $3==idx {count++} END {print count+0}' "$PIXEL_SAMPLER")
        echo "  s$i: $count" | tee -a "$SUMMARY"
    done
fi

# Compare attributes
if [ -f "$PIXEL_SAMPLER" ]; then
    for i in {0..7}; do
        count=$(awk -F'\t' -v idx="$i" '$2=="a" && $3==idx {count++} END {print count+0}' "$PIXEL_SAMPLER")
        echo "  a$i: $count" | tee -a "$SUMMARY"
    done
fi

echo "" | tee -a "$SUMMARY"
echo "Done. Raw data in $OUTPUT_DIR" | tee -a "$SUMMARY"