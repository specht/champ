#!/usr/bin/env ruby

require 'fileutils'
require 'open3'
require 'set'
require 'tmpdir'
require 'yaml'
require 'stringio'

KEYWORDS = <<EOS
    ADC AND ASL BCC BCS BEQ BIT BMI BNE BPL BRA BRK BVC BVS CLC CLD CLI CLV
    CMP CPX CPY DEA DEC DEX DEY DSK EOR EQU INA INC INX INY JMP JSR LDA LDX
    LDY LSR MX  NOP ORA ORG PHA PHP PHX PHY PLA PLP PLX PLY ROL ROR RTI RTS
    SBC SEC SED SEI STA STX STY STZ TAX TAY TRB TSB TSX TXA TXS TYA
EOS

# font data borrowed from http://sunge.awardspace.com/glcd-sd/node4.html
# Graphic LCD Font (Ascii Charaters 0x20-0x7F)
# Author: Pascal Stang, Date: 10/19/2001
FONT_DATA = <<EOS
    00 00 00 00 00 00 00 5F 00 00 00 07 00 07 00 14 7F 14 7F 14 24 2A 7F 2A 12
    23 13 08 64 62 36 49 55 22 50 00 05 03 00 00 00 1C 22 41 00 00 41 22 1C 00
    08 2A 1C 2A 08 08 08 3E 08 08 00 50 30 00 00 08 08 08 08 08 00 60 60 00 00
    20 10 08 04 02 3E 51 49 45 3E 00 42 7F 40 00 42 61 51 49 46 21 41 45 4B 31
    18 14 12 7F 10 27 45 45 45 39 3C 4A 49 49 30 01 71 09 05 03 36 49 49 49 36
    06 49 49 29 1E 00 36 36 00 00 00 56 36 00 00 00 08 14 22 41 14 14 14 14 14
    41 22 14 08 00 02 01 51 09 06 32 49 79 41 3E 7E 11 11 11 7E 7F 49 49 49 36
    3E 41 41 41 22 7F 41 41 22 1C 7F 49 49 49 41 7F 09 09 01 01 3E 41 41 51 32
    7F 08 08 08 7F 00 41 7F 41 00 20 40 41 3F 01 7F 08 14 22 41 7F 40 40 40 40
    7F 02 04 02 7F 7F 04 08 10 7F 3E 41 41 41 3E 7F 09 09 09 06 3E 41 51 21 5E
    7F 09 19 29 46 46 49 49 49 31 01 01 7F 01 01 3F 40 40 40 3F 1F 20 40 20 1F
    7F 20 18 20 7F 63 14 08 14 63 03 04 78 04 03 61 51 49 45 43 00 00 7F 41 41
    02 04 08 10 20 41 41 7F 00 00 04 02 01 02 04 40 40 40 40 40 00 01 02 04 00
    20 54 54 54 78 7F 48 44 44 38 38 44 44 44 20 38 44 44 48 7F 38 54 54 54 18
    08 7E 09 01 02 08 14 54 54 3C 7F 08 04 04 78 00 44 7D 40 00 20 40 44 3D 00
    00 7F 10 28 44 00 41 7F 40 00 7C 04 18 04 78 7C 08 04 04 78 38 44 44 44 38
    7C 14 14 14 08 08 14 14 18 7C 7C 08 04 04 08 48 54 54 54 20 04 3F 44 40 20
    3C 40 40 20 7C 1C 20 40 20 1C 3C 40 30 40 3C 44 28 10 28 44 0C 50 50 50 3C
    44 64 54 4C 44 00 08 36 41 00 00 00 7F 00 00 00 41 36 08 00 08 08 2A 1C 08
    08 1C 2A 08 08
EOS

FONT = FONT_DATA.split(/\s+/).map { |x| x.strip }.reject { |x| x.empty? }.map { |x| x.to_i(16) }

class Champ
    def initialize
        if ARGV.empty?
            STDERR.puts 'Usage: ./champ.rb [options] <config.yaml>'
            STDERR.puts 'Options:'
            STDERR.puts '  --max-frames <n>'
            STDERR.puts '  --no-animation'
            exit(1)
        end
        @files_dir = 'report-files'
        FileUtils.rm_rf(@files_dir)
        FileUtils.mkpath(@files_dir)
        @max_frames = nil
        @record_frames = true
        args = ARGV.dup
        while args.size > 1
            item = args.shift
            if item == '--max-frames'
                @max_frames = args.shift.to_i
            elsif item == '--no-animation'
                @record_frames = false
            else
                STDERR.puts "Invalid argument: #{item}"
                exit(1)
            end
        end
        @config = YAML.load(File.read(args.shift))
        @source_files = []
        @config['load'].each_pair do |address, path|
            @source_files << {:path => File.absolute_path(path), :address => address}
        end
        @highlight_color = '#fce98d'
        if @config['highlight']
            @highlight_color = @config['highlight']
        end

        @keywords = Set.new(KEYWORDS.split(/\s/).map { |x| x.strip }.reject { |x| x.empty? })
        @global_variables = {}
        @watches = {}
        @watches_for_index = []
        @label_for_pc = {}
        @pc_for_label = {}

        # init disk image to zeroes
        @disk_image = [0] * 0x10000
        # load empty disk image
        load_image('empty', 0)
        @source_files.each do |source_file|
            @source_path = File.absolute_path(source_file[:path])
            @source_line = 0
            unless File.exist?(@source_path)
                STDERR.puts 'Input file not found.'
                exit(1)
            end
            if @source_path[-2, 2] == '.s'
                Dir::mktmpdir do |temp_dir|
                    FileUtils.cp(@source_path, temp_dir)

                    pwd = Dir.pwd
                    Dir.chdir(temp_dir)
                    # compile the source
                    merlin_output = `Merlin32 -V . #{File.basename(@source_path)}`
                    if $?.exitstatus != 0 || File.exist?('error_output.txt')
                        STDERR.puts merlin_output
                        exit(1)
                    end
                    # collect Merlin output files
                    @merlin_output_path = File.absolute_path(Dir['*_Output.txt'].first)
                    @merlin_binary_path = @merlin_output_path.sub('_Output.txt', '')

                    # parse Merlin output files
                    # TODO: Adjust addresses!!!
                    parse_merlin_output(@merlin_output_path)
                    load_image(@merlin_binary_path, source_file[:address])
                    Dir.chdir(pwd)
                end
            else
                load_image(@source_path, source_file[:address])
            end
        end
    end

    def load_image(path, address)
#         puts "[#{sprintf('0x%04x', address)} - #{sprintf('0x%04x', address + File.size(path) - 1)}] - loading #{File.basename(path)}"
        File::binread(path).unpack('C*').each.with_index { |b, i| @disk_image[i + address] = b }
    end

    def run
        Dir::mktmpdir do |temp_dir|
            if @config.include?('instant_rts')
                @config['instant_rts'].each do |label|
                    @disk_image[@pc_for_label[label]] = 0x60 # insert RTS
                end
            end
            File.binwrite(File.join(temp_dir, 'disk_image'), @disk_image.pack('C*'))
            # build watch input for C program
            io = StringIO.new
            watch_index = 0
            @watches.keys.sort.each do |pc|
                @watches[pc].each do |watch0|
                    watch0[:components].each do |watch|
                        which = watch.include?(:register) ?
                            sprintf('reg,%s', watch[:register]) :
                            sprintf('mem,0x%04x', watch[:address])
                        io.puts sprintf('%d,0x%04x,%d,%s,%s',
                                        watch_index, pc,
                                        watch0[:post] ? 1 : 0,
                                        watch[:type],
                                        which)
                    end
                    @watches_for_index << watch0
                    watch_index += 1
                end
            end
            watch_input = io.string

            @watch_values = {}
            start_pc = @pc_for_label[@config['entry']] || @config['entry']
            Signal.trap('INT') do
                puts 'Killing 65C02 profiler...'
                throw :sigint
            end
            @frame_count = 0
            cycle_count = 0
            last_frame_time = 0
            frame_cycles = []
            @cycles_per_function = {}
            @calls_per_function = {}
            call_stack = []
            last_call_stack_cycles = 0
            Open3.popen2("./p65c02 --hide-log --start-pc #{start_pc} #{File.join(temp_dir, 'disk_image')}") do |stdin, stdout, thread|
                stdin.puts watch_input.split("\n").size
                stdin.puts watch_input
                stdin.close
                catch :sigint do
                    gi = nil
                    go = nil
                    gt = nil
                    if @record_frames
                        gi, go, gt = Open3.popen2("./pgif 280 192 2 > #{File.join(@files_dir, 'frames.gif')}")
                        gi.puts '000000'
                        gi.puts 'ffffff'
                    end
                    stdout.each_line do |line|
#                         puts "> #{line}"
                        parts = line.split(' ')
                        if parts.first == 'jsr'
                            pc = parts[1].to_i(16)
                            cycles = parts[2].to_i
                            @calls_per_function[pc] ||= 0
                            @calls_per_function[pc] += 1
                            unless call_stack.empty?
                                @cycles_per_function[call_stack.last] ||= 0
                                @cycles_per_function[call_stack.last] += cycles - last_call_stack_cycles
                            end
                            last_call_stack_cycles = cycles
                            call_stack << pc
                        elsif parts.first == 'rts'
                            cycles = parts[1].to_i
                            unless call_stack.empty?
                                @cycles_per_function[call_stack.last] ||= 0
                                @cycles_per_function[call_stack.last] += cycles - last_call_stack_cycles
                            end
                            last_call_stack_cycles = cycles
                            call_stack.pop
                        elsif parts.first == 'watch'
                            watch_index = parts[1].to_i
                            @watch_values[watch_index] ||= []
                            @watch_values[watch_index] << parts[2, parts.size - 2].map { |x| x.to_i }
                        elsif parts.first == 'screen'
                            @frame_count += 1
                            print "\rFrames: #{@frame_count}, Cycles: #{cycle_count}"
                            this_frame_cycles = parts[1].to_i
                            frame_cycles << this_frame_cycles
                            if @record_frames
                                data = parts[2, parts.size - 2].map { |x| x.to_i }
                                gi.puts 'l'
                                (0...192).each do |y|
                                    (0...280).each do |x|
                                        b = (data[y * 40 + (x / 7)] >> (x % 7)) & 1
                                        gi.print b
                                    end
                                    gi.puts
                                end

                                gi.puts "d #{(this_frame_cycles - last_frame_time) / 10000}"
                            end
                            last_frame_time = this_frame_cycles

                            if @max_frames && @frame_count >= @max_frames
                                break
                            end
                        elsif parts.first == 'cycles'
                            cycle_count = parts[1].to_i
                            print "\rFrames: #{@frame_count}, Cycles: #{cycle_count}"
                        end
                    end
                    if @record_frames
                        gi.close
                        gt.join
                    end
                end
            end
            puts

            @cycles_per_frame = []
            (2...frame_cycles.size).each do |i|
                @cycles_per_frame << frame_cycles[i] - frame_cycles[i - 1]
            end
        end
    end

    def print_c(pixels, width, height, x, y, c, color)
        if c.ord >= 0x20 && c.ord < 0x80
            font_index = (c.ord - 0x20) * 5
            (0...5).each do |px|
                (0...7).each do |py|
                    if ((FONT[font_index + px] >> py) & 1) == 1
                        pixels[(y + py) * width + (x + px)] = color
                    end
                end
            end
        end
    end

    def print_c_r(pixels, width, height, x, y, c, color)
        if c.ord >= 0x20 && c.ord < 0x80
            font_index = (c.ord - 0x20) * 5
            (0...5).each do |px|
                (0...7).each do |py|
                    if ((FONT[font_index + px] >> (6 - py)) & 1) == 1
                        pixels[(y + px) * width + (x + py)] = color
                    end
                end
            end
        end
    end

    def print_s(pixels, width, height, x, y, s, color)
        s.each_char.with_index do |c, i|
            print_c(pixels, width, height, x + i * 6, y, c, color)
        end
    end

    def print_s_r(pixels, width, height, x, y, s, color)
        s.each_char.with_index do |c, i|
            print_c_r(pixels, width, height, x, y + i * 6, c, color)
        end
    end

    def write_report
        html_name = 'report.html'
        print "Writing report to file://#{File.absolute_path(html_name)} ..."
        File::open(html_name, 'w') do |f|
            report = DATA.read

            # write frames
            io = StringIO.new
            if @record_frames
                io.puts "<img class='screenshot' src='#{File.join(@files_dir, 'frames.gif')}' /><br />"
            end
            if @cycles_per_frame.size > 0
                io.puts '<p>'
                io.puts "Frames recorded: #{@frame_count}<br />"
                io.puts "Average cycles/frame: #{@cycles_per_frame.inject(0) { |sum, x| sum + x } / @cycles_per_frame.size}<br />"
                io.puts '<p>'
            end
            report.sub!('#{screenshots}', io.string)

            # write watches
            io = StringIO.new
            @watches_for_index.each.with_index do |watch, index|
                io.puts "<div style='display: inline-block;'>"
                if @watch_values.include?(index)
                    pixels = nil
                    width = nil
                    height = nil
                    histogram = {}
                    mask = [
                        [0,0,1,1,1,0,0],
                        [0,1,1,1,1,1,0],
                        [1,1,1,1,1,1,1],
                        [1,1,1,1,1,1,1],
                        [1,1,1,1,1,1,1],
                        [0,1,1,1,1,1,0],
                        [0,0,1,1,1,0,0]
                    ]
                    @watch_values[index].each do |item|
                        normalized_item = []
                        item.each.with_index do |x, i|
                            if watch[:components][i][:type] == 's8'
                                x += 128
                            elsif watch[:components][i][:type] == 'u16'
                                x >>= 8
                            elsif watch[:components][i][:type] == 's16'
                                x = (x + 32768) >> 8
                            end
                            normalized_item << x
                        end
                        offset = normalized_item.reverse.inject(0) { |x, y| (x << 8) + y }
                        histogram[offset] ||= 0
                        histogram[offset] += 1
                    end

                    histogram_max = histogram.values.max
                    width = 276
                    height = (watch[:components].size == 1) ? 148 : 276
                    canvas_top = 20
                    canvas_left = 40
                    canvas_width = width - 60
                    canvas_height = height - 60
                    pixels = [0] * width * height

                    histogram.each_pair do |key, value|
                        x = key & 0xff;
                        y = (((key ^ 0xff) >> 8) & 0xff)
                        x = (x * canvas_width) / 255 + canvas_left
                        y = (y * canvas_height) / 255 + canvas_top
                        ymin = y
                        ymax = y
                        if watch[:components].size == 1
                            ymin = canvas_top + canvas_height - 1 - (value.to_f * (canvas_height - 1) / histogram_max).to_i
                            ymax = canvas_top + canvas_height - 1
                        end
                        (ymin..ymax).each do |y|
                            (0..6).each do |dy|
                                py = y + dy - 3
                                if py >= 0 && py < height
                                    (0..6).each do |dx|
                                        next if mask[dy][dx] == 0
                                        px = x + dx - 3
                                        if px >= 0 && px < width
                                            if pixels[py * width + px] == 0
                                                pixels[py * width + px] = 1
                                            end
                                        end
                                    end
                                end
                            end
                            pixels[y * width + x] = (((value.to_f / histogram_max) ** 0.5) * 63).to_i
                        end
                    end

                    watch[:components].each.with_index do |component, component_index|
                        labels = []
                        if component[:type] == 'u8'
                            labels << [0.0, '0']
                            labels << [64.0/255, '64']
                            labels << [128.0/255, '128']
                            labels << [192.0/255, '192']
                            labels << [1.0, '255']
                        elsif component[:type] == 's8'
                            labels << [0.0, '-128']
                            labels << [64.0/255, '-64']
                            labels << [128.0/255, '0']
                            labels << [192.0/255, '64']
                            labels << [1.0, '127']
                        elsif component[:type] == 'u16'
                            labels << [0.0, '0']
                            labels << [64.0/255, '16k']
                            labels << [128.0/255, '32k']
                            labels << [192.0/255, '48k']
                            labels << [1.0, '64k']
                        elsif component[:type] == 's16'
                            labels << [0.0, '-32k']
                            labels << [64.0/255, '-16k']
                            labels << [128.0/255, '0']
                            labels << [192.0/255, '16k']
                            labels << [1.0, '32k']
                        end
                        labels.each do |label|
                            s = label[1]
                            if component_index == 0
                                x = (label[0] * canvas_width).to_i + canvas_left
                                print_s(pixels, width, height,
                                        (x - s.size * (6 * label[0])).to_i,
                                        canvas_top + canvas_height + 7, s, 63)
                                (0..(canvas_height + 3)).each do |y|
                                    pixels[(y + canvas_top) * width + x] |= 0x40
                                end
                            else
                                y = ((1.0 - label[0]) * canvas_height).to_i + canvas_top
                                print_s_r(pixels, width, height, canvas_left - 12,
                                            (y - s.size * (6 * (1.0 - label[0]))).to_i, s, 63)
                                (-3..canvas_width).each do |x|
                                    pixels[y * width + (x + canvas_left)] |= 0x40
                                end
                            end
                        end
                        (0..1).each do |offset|
                            component_label = component[:name]
                            if component_index == 0
                                print_s(pixels, width, height,
                                        (canvas_left + canvas_width * 0.5 - component_label.size * 3 + offset).to_i,
                                        canvas_top + canvas_height + 18,
                                        component_label, 63)
                            else
                                print_s_r(pixels, width, height,
                                            canvas_left - 22,
                                            (canvas_top + canvas_height * 0.5 - component_label.size * 3 + offset).to_i,
                                            component_label, 63)
                            end
                        end
                    end
                    label = "#{sprintf('0x%04x', watch[:pc])} / #{watch[:path]}:#{watch[:line_number]} (#{watch[:post] ? 'post' : 'pre'})"
                    print_s(pixels, width, height, width / 2 - 3 * label.size, height - 10, label, 63)

                    tr = @highlight_color[1, 2].to_i(16)
                    tg = @highlight_color[3, 2].to_i(16)
                    tb = @highlight_color[5, 2].to_i(16)

                    if pixels
                        gi, go, gt = Open3.popen2("./pgif #{width} #{height} 128")
                        palette = [''] * 128
                        (0...64).each do |i|
                            if (i == 0)
                                r = 0xff
                                g = 0xff
                                b = 0xff
                            else
                                l = (((63 - i) + 1) << 2) - 1
                                r = l * tr / 255
                                g = l * tg / 255
                                b = l * tb / 255
                            end
                            palette[i] = sprintf('%02x%02x%02x', r, g, b)
                            r = r * 4 / 5
                            g = g * 4 / 5
                            b = b * 4 / 5
                            palette[i + 64] = sprintf('%02x%02x%02x', r, g, b)
                        end
                        gi.puts palette.join("\n")
                        gi.puts 'f'
                        gi.puts pixels.map { |x| sprintf('%02x', x) }.join("\n")
                        gi.close
                        gt.join
                        watch_path = File.join(@files_dir, "watch_#{index}.gif")
                        File::open(watch_path, 'w') do |f|
                            f.write go.read
                        end
                        io.puts "<img src='#{watch_path}'></img>"
                    end
                else
                    io.puts "<em>No values recorded.</em>"
                end
                io.puts "</div>"
            end
            report.sub!('#{watches}', io.string)

            # write cycles
            io = StringIO.new
            io.puts "<table>"
            io.puts "<thead>"
            io.puts "<tr>"
            io.puts "<th>Addr</th>"
            io.puts "<th>CC</th>"
            io.puts "<th>CC %</th>"
            io.puts "<th>Calls</th>"
            io.puts "<th>CC/Call</th>"
            io.puts "<th>Label</th>"
            io.puts "</tr>"
            io.puts "</thead>"
            cycles_sum = @cycles_per_function.values.inject(0) { |a, b| a + b }
            @cycles_per_function.keys.sort do |a, b|
                @cycles_per_function[b] <=> @cycles_per_function[a]
            end.each do |pc|
                io.puts "<tr>"
                io.puts "<td>#{sprintf('0x%04x', pc)}</td>"
                io.puts "<td style='text-align: right;'>#{@cycles_per_function[pc]}</td>"
                io.puts "<td style='text-align: right;'>#{sprintf('%1.2f%%', @cycles_per_function[pc].to_f * 100.0 / cycles_sum)}</td>"
                io.puts "<td style='text-align: right;'>#{@calls_per_function[pc]}</td>"
                io.puts "<td style='text-align: right;'>#{@cycles_per_function[pc] / @calls_per_function[pc]}</td>"
                io.puts "<td>#{@label_for_pc[pc]}</td>"
                io.puts "</tr>"

            end
            io.puts "</table>"
            report.sub!('#{cycles}', io.string)

            f.puts report
        end
        puts ' done.'
    end

    def parse_asm_int(s)
        if s[0] == '#'
            s[1, s.size - 1].to_i
        elsif s[0] == '$'
            s[1, s.size - 1].to_i(16)
        else
            s.to_i
        end
    end

    def parse_merlin_output(path)
        @source_line = -3
        File.open(path, 'r') do |f|
            f.each_line do |line|
                @source_line += 1
                parts = line.split('|')
                next unless parts.size > 2
                line_type = parts[2].strip
                if ['Code', 'Equivalence', 'Empty'].include?(line_type)
                    next if parts[6].nil?
                    next unless parts[6] =~ /[0-9A-F]{2}\/[0-9A-F]{4}/
                    pc = parts[6].split(' ').first.split('/').last.to_i(16)
                    code = parts[7].strip
                    code_parts = code.split(/\s+/)
                    next if code_parts.empty?

                    label = nil

                    champ_directives = []
                    if code.include?(';')
                        comment = code[code.index(';') + 1, code.size].strip
                        comment.scan(/@[^\s]+/).each do |match|
                            champ_directives << match.to_s
                        end
                    end
                    if champ_directives.empty? && line_type == 'Equivalence'
                        label = code_parts[0]
                        pc = parse_asm_int(code_parts[2])
                        @label_for_pc[pc] = label
                        @pc_for_label[label] = pc
                    else
                        unless @keywords.include?(code_parts.first)
                            label = code_parts.first
                            code = code.sub(label, '').strip
                            @label_for_pc[pc] = label
                            @pc_for_label[label] = pc
                        end
                    end

                    next if champ_directives.empty?

                    if line_type == 'Equivalence'
                        if champ_directives.size != 1
                            fail('No more than one champ directive allowed in equivalence declaration.')
                        end
                        item = {
                            :address => code_parts[2].sub('$', '').to_i(16),
                            :type => parse_champ_directive(champ_directives.first, true)[:type]
                        }
                        @global_variables[label] = item
                    elsif line_type == 'Code'
                        @watches[pc] ||= []
                        champ_directives.each do |directive|
                            line_number = parts[1].split(' ').map { |x| x.strip }.reject { |x| x.empty? }.last.to_i
                            watch = parse_champ_directive(directive, false)
                            watch[:line_number] = line_number
                            watch[:pc] = pc
                            @watches[pc] << watch
                        end
                    end
                else
                    if line.include?(';')
                        if line.split(';').last.include?('@')
                            fail('Champ directive not allowed here.')
                        end
                    end
                end
            end
        end
    end

    def fail(message)
        STDERR.puts sprintf('[%s:%d] %s', File::basename(@source_path), @source_line, message)
        exit(1)
    end

    def parse_champ_directive(s, global_variable = false)
        original_directive = s.dup
        # Au Au(post) As Xs(post) RX u8 Au,Xu,Yu
        result = {}
        s = s[1, s.size - 1] if s[0] == '@'
        result[:path] = File.basename(@source_path)
        if global_variable
            if ['u8', 's8', 'u16', 's16'].include?(s)
                result[:type] = s
            else
                fail("Error parsing champ directive: #{original_directive}")
            end
        else
            if s.include?('(post)')
                result[:post] = true
                s.sub!('(post)', '')
            end
            result[:components] = s.split(',').map do |part|
                part_result = {}
                if ['Au', 'As', 'Xu', 'Xs', 'Yu', 'Ys'].include?(part[0, 2])
                    part_result[:register] = part[0]
                    part_result[:name] = part[0]
                    part_result[:type] = part[1] + '8'
                elsif @global_variables.include?(part)
                    part_result[:address] = @global_variables[part][:address]
                    part_result[:type] = @global_variables[part][:type]
                    part_result[:name] = part
                else
                    fail("Error parsing champ directive: #{original_directive}")
                    exit(1)
                end
                part_result
            end
            if result[:components].size > 2
                fail("No more than two components allowed per watch in #{original_directive}")
            end
        end
        result
    end
end

['p65c02', 'pgif'].each do |file|
    unless FileUtils.uptodate?(file, ["#{file}.c"])
        system("gcc -o #{file} #{file}.c")
        unless $?.exitstatus == 0
            exit(1)
        end
    end
end

champ = Champ.new
champ.run
champ.write_report

__END__

<html>
<head>
    <title>champ report</title>
    <style type='text/css'>
    body {
        background-color: #eee;
        font-family: monospace;
    }
    .screenshot {
        background-color: #222;
        box-shadow: inset 0 0 10px rgba(0,0,0,1.0);
        padding: 12px;
        border-radius: 8px;
    }
    th, td {
        text-align: left;
        padding: 0 0.5em;
    }
    </style>
</head>
<body>
<div style='float: left; border-right: 1px solid #aaa;'>
    <h2>Frames</h2>
    #{screenshots}
    <h2>Cycles</h2>
    #{cycles}
</div>
<div style='margin-left: 440px; padding-top: 5px;'>
    <h2>Watches</h2>
    #{watches}
</div>
</body>
</html>
