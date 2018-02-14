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
            Dir::mktmpdir do |temp_dir|
                FileUtils.cp(@source_path, temp_dir)

                pwd = Dir.pwd
                Dir.chdir(temp_dir)
                if @source_path[-2, 2] == '.s'
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
                else
                    load_image(@source_path, source_file[:address])
                end
                Dir.chdir(pwd)
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

    def write_report
        html_name = 'report.html'
        print "Writing report to file://#{File.absolute_path(html_name)} ..."
        File::open(html_name, 'w') do |f|
            report = DATA.read
            report.sub!('#{source_name}', File.basename(@source_path))

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
                io.puts "<h3>#{watch[:components].map { |x| x[:name] }.join('/')} (#{File.basename(@source_path)}:#{watch[:line_number]})</h3>"
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
                    width = 256
                    height = (watch[:components].size == 1) ? 128 : 256
                    pixels = [64] * width * height

                    histogram.each_pair do |key, value|
                        x = key & 0xff;
                        y = height - 1 - ((key >> 8) & 0xff)
                        ymin = y
                        ymax = y
                        if watch[:components].size == 1
                            ymin = height - 1 - (value.to_f * (height - 1) / histogram_max).to_i
                            ymax = height - 1
                        end
                        (ymin..ymax).each do |y|
                            (0..6).each do |dy|
                                py = y + dy - 3
                                if py >= 0 && py < height
                                    (0..6).each do |dx|
                                        next if mask[dy][dx] == 0
                                        px = x + dx - 3
                                        if px >= 0 && px < width
                                            if pixels[py * width + px] == 64
                                                pixels[py * width + px] = 0
                                            end
                                        end
                                    end
                                end
                            end
                            pixels[y * width + x] = (((value.to_f / histogram_max) ** 0.5) * 63).to_i
                        end
                    end
#                     if watch[:components].size == 1
#                         @watch_values[index].each do |item|
#                             if watch[:components][0][:type] == 's8'
#                                 item[0] += 128
#                             elsif watch[:components][0][:type] == 'u16'
#                                 item[0] >>= 8
#                             elsif watch[:components][0][:type] == 's16'
#                                 item[0] = (item[0] + 32768) >> 8
#                             end
#                             histogram[item] ||= 0
#                             histogram[item] += 1
#                         end
#                         histogram_max = histogram.values.max
#                         width = 256
#                         height = 128
#                         pixels = [64] * width * height
#                         histogram.each_pair do |key, value|
#                             key_parts = key
#                             (0..(((value.to_f / histogram_max) * 127).to_i)).each do |y|
#                                 x = key_parts[0]
#                                 pixels[(127 - y) * width + x] = (((value.to_f / histogram_max) ** 0.5) * 63).to_i
#                             end
#                         end
#                     elsif watch[:components].size == 2
#                         @watch_values[index].each do |item|
#                             normalized_item = []
#                             item.each.with_index do |x, i|
#                                 if watch[:components][i][:type] == 's8'
#                                     x += 128
#                                 elsif watch[:components][i][:type] == 'u16'
#                                     x >>= 8
#                                 elsif watch[:components][i][:type] == 's16'
#                                     x = (x + 32768) >> 8
#                                 end
#                                 normalized_item << x
#                             end
#                             offset = (normalized_item[1] << 8) + normalized_item[0]
#                             histogram[offset] ||= 0
#                             histogram[offset] += 1
#                         end
#                         histogram_max = histogram.values.max
#                         width = 256
#                         height = 256
#                         pixels = [64] * width * height
#                         histogram.each_pair do |key, value|
#                             x = key & 0xff;
#                             y = 255 - ((key >> 8) & 0xff)
#                             (0..6).each do |dy|
#                                 py = y + dy - 3
#                                 if py >= 0 && py < height
#                                     (0..6).each do |dx|
#                                         next if mask[dy][dx] == 0
#                                         px = x + dx - 3
#                                         if py >= 0 && py < height
#                                             if pixels[py * width + px] == 64
#                                                 pixels[py * width + px] = 0
#                                             end
#                                         end
#                                     end
#                                 end
#                             end
#                             pixels[y * width + x] = (((value.to_f / histogram_max) ** 0.5) * 63).to_i
#                         end
#                     end

                    if pixels
                        gi, go, gt = Open3.popen2("./pgif #{width} #{height} 65")
                        (0...64).each do |i|
                            l = (((63 - i) + 1) << 2) - 1
                            # fce98d
                            r = 0xfc
                            g = 0xe9
                            b = 0x8d
                            gi.puts sprintf('%02x%02x%02x',
                                            l * r / 255,
                                            l * g / 255,
                                            l * b / 255)
                        end
                        gi.puts 'ffffff'
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
                            watch = parse_champ_directive(directive, false)
                            watch[:line_number] = @source_line
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
<h1>champ report for #{source_name}</h1>
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
