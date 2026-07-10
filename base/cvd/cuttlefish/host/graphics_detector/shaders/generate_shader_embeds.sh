for file in *.{frag,vert}; do
    [ -f "${file}" ] || break

    SHADER_GLSL="${file}"
    echo "Found ${SHADER_GLSL}"

    SHADER_SPV="${file}.spv"

    glslc \
        "${SHADER_GLSL}" \
        -o "${SHADER_SPV}"

    echo "Generated ${SHADER_SPV}"
done
