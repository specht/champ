#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#pragma pack(push, 1)

#define ANIMATION_OPTIMIZATION_CROP
// don't use transparency, it's not saving space with 1 bit graphics
// #define ANIMATION_OPTIMIZATION_TRANSPARENCY

struct header_block {
    char signature[3];
    char version[3];
};

struct logical_screen_descriptor {
    uint16_t canvas_width;
    uint16_t canvas_height;
    uint8_t size_of_global_color_table: 3;
    uint8_t sort_flag: 1;
    uint8_t color_resolution: 3;
    uint8_t global_color_table_flag: 1;
    uint8_t background_color_index;
    uint8_t pixel_aspect_ratio;
};

struct graphics_control_extension {
    uint8_t extension_introducer;
    uint8_t graphics_control_label;
    uint8_t byte_size;
    uint8_t transparent_color_flag: 1;
    uint8_t user_input_flag: 1;
    uint8_t disposal_method: 3;
    uint8_t reserved: 3;
    uint16_t delay_time;
    uint8_t transparent_color_index;
    uint8_t block_terminator;
};

struct image_descriptor {
    uint8_t image_separator;
    uint16_t image_left;
    uint16_t image_top;
    uint16_t image_width;
    uint16_t image_height;
    uint8_t size_of_local_color_table: 3;
    uint8_t reserved: 2;
    uint8_t sort_flag: 1;
    uint8_t interlace_flag: 1;
    uint8_t local_color_table_flag: 1;
};

struct application_extension {
    uint8_t gif_extension_code;
    uint8_t application_extension_label;
    uint8_t length_of_application_block;
    char label[11];
    uint8_t length_of_data_sub_block;
    uint8_t one;
    uint16_t loop_count;
    uint8_t terminator;
};

#pragma pack(pop)

void put8(uint8_t i)
{
    fputc(i, stdout);
}

void put_struct(void* p, size_t size)
{
    for (int i = 0; i < size; i++)
        put8(*(unsigned char*)(p + i));
}

struct lzw_emitter {
    uint8_t byte_buffer[255];
    uint8_t byte_buffer_size;
    uint32_t buffer;
    int8_t offset;
    uint8_t code_size;
};

void flush_bytes(struct lzw_emitter* emitter)
{
    put8(emitter->byte_buffer_size);
    put_struct(emitter->byte_buffer, emitter->byte_buffer_size);
    emitter->byte_buffer_size = 0;
}

void emit_byte(struct lzw_emitter* emitter)
{
    emitter->byte_buffer[emitter->byte_buffer_size++] = emitter->buffer & 0xff;
    emitter->buffer >>= 8;
    emitter->offset -= 8;
    if (emitter->byte_buffer_size == 0xff)
        flush_bytes(emitter);
}

void emit_code(struct lzw_emitter* emitter, uint32_t code)
{
    emitter->buffer |= code << emitter->offset;
    emitter->offset += emitter->code_size;
    while (emitter->offset > 7)
        emit_byte(emitter);
}

void flush_emitter(struct lzw_emitter* emitter)
{
    while (emitter->offset > 0)
        emit_byte(emitter);
    if (emitter->byte_buffer_size > 0)
        flush_bytes(emitter);
}

void encode_image(uint8_t* pixels, uint8_t* previous_pixels,
                  uint16_t width, uint16_t height,
                  uint8_t colors_used, uint16_t frame_delay)
{
    uint8_t color_depth = 1;
    while (colors_used > (1 << color_depth))
        color_depth++;
    if (color_depth < 2)
        color_depth = 2;

    uint16_t cropped_left = 0;
    uint16_t cropped_top = 0;
    uint16_t cropped_width = width;
    uint16_t cropped_height = height;
    int16_t transparent_color = -1;

    if (previous_pixels)
    {
        // if previous_pixels is not null, we can do some differential encoding!

        #ifdef ANIMATION_OPTIMIZATION_CROP
        // crop left and right
        for (int dir = 0; dir < 2; dir++)
        {
            while (cropped_width > 0)
            {
                uint8_t same_pixels = 1;
                int x, y;
                x = (dir == 0) ? cropped_left : cropped_left + cropped_width - 1;
                for (int y = cropped_top; same_pixels && (y < cropped_top + cropped_height); y++)
                {
                    int offset = y * width + x;
                    if (pixels[offset] != previous_pixels[offset])
                        same_pixels = 0;
                }
                if (same_pixels)
                {
                    if (dir == 0)
                        cropped_left++;
                    cropped_width--;
                }
                else
                    break;
            }
        }

        // crop top and bottom
        for (int dir = 0; dir < 2; dir++)
        {
            while (cropped_height > 0)
            {
                uint8_t same_pixels = 1;
                int x, y;
                y = (dir == 0) ? cropped_top : cropped_top + cropped_height - 1;
                for (int x = cropped_left; same_pixels && (x < cropped_left + cropped_width); x++)
                {
                    int offset = y * width + x;
                    if (pixels[offset] != previous_pixels[offset])
                        same_pixels = 0;
                }
                if (same_pixels)
                {
                    if (dir == 0)
                        cropped_top++;
                    cropped_height--;
                }
                else
                    break;
            }
        }
        #endif

        #ifdef ANIMATION_OPTIMIZATION_TRANSPARENCY
        // replace unchanged pixels with tranparency
        if (colors_used < 255)
        {
            transparent_color = colors_used;
            colors_used++;
            while (colors_used >= (1 << color_depth))
                color_depth++;
            for (int y = 0; y < cropped_height; y++)
            {
                uint32_t offset = (y + cropped_top) * width + cropped_left;
                uint8_t* p = pixels + offset;
                uint8_t* pp = previous_pixels + offset;
                for (int x = 0; x < cropped_width; x++)
                {
                    if (*p == *pp)
                    {
                        *p = (uint8_t)transparent_color;
                    }
                    p++;
                    pp++;
                }
            }
        }
        #endif
    }
    // write graphics control extension
    struct graphics_control_extension gce;
    memset(&gce, 0, sizeof(gce));
    gce.extension_introducer = 0x21;
    gce.graphics_control_label = 0xf9;
    gce.byte_size = 4;
    gce.disposal_method = 1; // draw on top
    gce.delay_time = frame_delay; // in 1/100 seconds

    #ifdef ANIMATION_OPTIMIZATION_TRANSPARENCY
    if (transparent_color >= 0)
    {
        gce.transparent_color_flag = 1;
        gce.transparent_color_index = transparent_color;
    }
    #endif
    put_struct(&gce, sizeof(gce));

    // write image descriptor
    struct image_descriptor id;
    memset(&id, 0, sizeof(id));
    id.image_separator = 0x2c;
    id.image_left = cropped_left;
    id.image_top = cropped_top;
    id.image_width = cropped_width;
    id.image_height = cropped_height;
    put_struct(&id, sizeof(id));

    uint8_t lzw_minimum_code_size = color_depth;
//     if (lzw_minimum_code_size < 2)
//         lzw_minimum_code_size = 2;

    put8(lzw_minimum_code_size);

    struct lzw_emitter emitter;
    emitter.byte_buffer_size = 0;
    emitter.buffer = 0;
    emitter.offset = 0;
    emitter.code_size = lzw_minimum_code_size + 1;

    // set up LZW encoder
    uint16_t clear_code = (1 << color_depth);
    uint16_t end_of_information_code = clear_code + 1;

    uint16_t* prefix_table = malloc(sizeof(uint16_t) * (2048 - 1 - end_of_information_code));
    uint8_t* suffix_table = malloc(sizeof(uint8_t) * (2048 - 1 - end_of_information_code));
    uint16_t table_length = 0;

    emit_code(&emitter, clear_code);

    uint8_t* p = pixels;
    uint16_t index_buffer = 0;
    for (int y = 0; y < cropped_height; y++)
    {
        uint8_t* p = pixels + (y + cropped_top) * width + cropped_left;
        for (int x = 0; x < cropped_width; x++)
        {
            if (x == 0 && y == 0)
            {
                index_buffer = (uint16_t)(*(p++));
                continue;
            }
            uint8_t k = *(p++);
            uint16_t found_table_entry = 0xffff;
            for (int i = 0; i < table_length; i++)
                if (index_buffer == prefix_table[i] && k == suffix_table[i])
                    found_table_entry = i + end_of_information_code + 1;
            if (found_table_entry < 0xffff)
                index_buffer = found_table_entry;
            else
            {
                prefix_table[table_length] = index_buffer;
                suffix_table[table_length] = k;
                table_length += 1;
                if (table_length + end_of_information_code > (1 << emitter.code_size))
                    emitter.code_size++;
                emit_code(&emitter, index_buffer);
                index_buffer = k;
                if (table_length >= 2048 - 1 - end_of_information_code)
                {
                    emit_code(&emitter, clear_code);
                    table_length = 0;
                    emitter.code_size = lzw_minimum_code_size + 1;
                }
            }
        }
    }

    emit_code(&emitter, index_buffer);
    emit_code(&emitter, end_of_information_code);
    flush_emitter(&emitter);

    free(suffix_table);
    free(prefix_table);

    put8(0);
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "This program creates an animated GIF from a series of frames\n");
        fprintf(stderr, "passed via stdin.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage: ./pgif <width> <height> <number of colors>\n");
        fprintf(stderr, "  <width>            1 to 65535\n");
        fprintf(stderr, "  <height>           1 to 65535\n");
        fprintf(stderr, "  <number of colors> 1 to 255\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "After the program has started, write to its stdin:\n");
        fprintf(stderr, "- the palette (one color per line, HTML notation without #)\n");
        fprintf(stderr, "  example: '000000\\nffffff\\n' if you specified two colors\n");
        fprintf(stderr, "- as many frames as you wish, one per line, formatted as\n");
        fprintf(stderr, "  one big hex string and starting with f\n");
        fprintf(stderr, "  example: 'f 000100000100\\n' if you specified a 3x2 image\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "The default frame delay is 100 ms. You may change the frame delay\n");
        fprintf(stderr, "for all following frames by specifying 'd <number>\\n' where\n");
        fprintf(stderr, "<number> is a decimal number and specifies the delay in 1/100 seconds.\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "To finalize the GIF, close the stdin stream. Output is written to stdout.\n");
        exit(1);
    }
    char* temp = 0;
    uint16_t width = strtol(argv[1], &temp, 0);
    uint16_t height = strtol(argv[2], &temp, 0);
    uint8_t colors_used = strtol(argv[3], &temp, 0);

    uint8_t color_depth = 1;
    while (colors_used > (1 << color_depth))
        color_depth++;
    if (color_depth < 2)
        color_depth = 2;

    // write header
    struct header_block header;
    strncpy(header.signature, "GIF", 3);
    strncpy(header.version, "89a", 3);
    put_struct(&header, sizeof(header));

    // write logical screen descriptor
    struct logical_screen_descriptor lsd;
    memset(&lsd, 0, sizeof(lsd));
    lsd.canvas_width = width;
    lsd.canvas_height = height;
    lsd.size_of_global_color_table = color_depth - 1;
    lsd.color_resolution = color_depth - 1;
    lsd.global_color_table_flag = 1;
    put_struct(&lsd, sizeof(lsd));

    size_t max_line_size = width + 1024;
    char* line = malloc(max_line_size);
    if (!line)
    {
        fprintf(stderr, "Error allocating line!\n");
        exit(1);
    }
    char* line_p;
    char hex[4];
    memset(hex, 0, 4);

    // write global color table, transfer palette from stdin to GIF
    for (int i = 0; i < colors_used; i++)
    {
        fgets(line, max_line_size, stdin);
        line_p = line;
        for (int k = 0; k < 3; k++)
        {
            strncpy(hex, line_p, 2);
            put8(strtol(hex, &temp, 16));
            line_p += 2;
        }
    }

    // fill remaining colors, if any
    for (int i = colors_used; i < (1 << color_depth); i++)
    {
        put8(0); put8(0); put8(0);
    }

    // write application extension NETSCAPE2.0 to loop the animation
    // (otherwise it just plays once, duh...)
    struct application_extension ae;
    memset(&ae, 0, sizeof(ae));
    ae.gif_extension_code = 0x21;
    ae.application_extension_label = 0xff;
    ae.length_of_application_block = 0x0b;
    strncpy(ae.label, "NETSCAPE2.0", 0x0b);
    ae.length_of_data_sub_block = 3;
    ae.one = 1;
    put_struct(&ae, sizeof(ae));

    uint8_t *previous_pixels = 0;

    uint8_t* pixels = malloc(width * height);
    if (!pixels)
    {
        fprintf(stderr, "Error allocating buffer for image.\n");
        exit(1);
    }

    uint16_t frame_delay = 10;
    while (fgets(line, max_line_size, stdin))
    {
        if (line[0] == 'f' || line[0] == 'l')
        {
            uint8_t* p = pixels;
            if (line[0] == 'f')
            {
                for (int y = 0; y < height; y++)
                {
                    for (int x = 0; x < width; x++)
                    {
                        fgets(hex, 4, stdin);
                        *(p++) = strtol(hex, &temp, 16);
                    }
                }
            }
            else if (line[0] == 'l')
            {
                for (int y = 0; y < height; y++)
                {
                    fgets(line, max_line_size, stdin);
                    line_p = line;
                    hex[1] = 0;
                    for (int x = 0; x < width; x++)
                    {
                        hex[0] = *(line_p++);
                        *(p++) = strtol(hex, &temp, 16);
                    }
                }
            }
            encode_image(pixels, previous_pixels, width, height, colors_used, frame_delay);
            if (!previous_pixels)
            {
                previous_pixels = malloc(width * height);
                if (!previous_pixels)
                {
                    fprintf(stderr, "Error allocating buffer for image.\n");
                    exit(1);
                }
            }
            memcpy(previous_pixels, pixels, width * height);
        }
        else if (line[0] == 'd')
            frame_delay = strtol(line + 2, &temp, 0);
    }

    if (previous_pixels)
        free(previous_pixels);
    free(pixels);
    free(line);

    // write trailer
    put8(0x3b);

    return 0;
}
