#!/usr/bin/env ruby

require 'yaml'

label_for_address = {}
symbols = []
watches = []
arg_watches = []

File::open('plot3d20_Output.txt') do |f|
    f.each_line do |line|
        parts = line.split('|')
        next unless parts.size == 8
        address = parts[6].split(' ').first.split('/')[1]
        symbol = parts[7]
        symbol = symbol[1, symbol.size - 1]
        next if symbol[0] == '*'
        next if symbol[0] == ';'
        next if symbol[0] == ' '
        if symbol =~ /^\w+\s+EQU\s+\$([0-9A-F]{2,4})/
            address = $1
        end
        watch_this = symbol.match(/~(u8|s8|u16|s16)/)
        watch_args = symbol.scan(/(@(u8|s8|u16|s16)[AXY])/).map { |x| x.first }
        symbol = symbol.strip.split(' ').first
        next if address.nil?
        label_for_address[address.to_i(16)] = symbol
        symbols << symbol
        if watch_this
            watches << {
                :address => address.to_i(16),
                :label => symbol,
                :type => watch_this[1]
            }
        end
        watch_args.each do |watch_arg|
            arg_watches << {:register => watch_arg[watch_arg.size - 1],
                            :type => watch_arg.sub('@', '')[0, watch_arg.size - 2],
                            :address => address.to_i(16)}
        end
    end
end

symbols.uniq!
symbols.sort! { |a, b| a.downcase <=> b.downcase }

File::open('labels.h', 'w') do |f|
    f.puts "const uint16_t LABEL_COUNT = #{symbols.size};"
    f.puts "const char* LABELS[] = {";
    line_length = 0
    f.print '    '
    symbols.each.with_index do |symbol, _|
        part = "\"#{symbol}\""
        if _ < symbols.size - 1
            part += ', '
        end
        f.print part
        line_length += part.size
        if line_length > 60
            f.puts
            f.print '    '
            line_length = 0
        end
    end
    f.puts "};"

    f.puts "const int16_t label_for_address[] = {"
    f.print '    '
    (0..0xffff).each do |address|
        value = -1
        if label_for_address.include?(address)
            value = symbols.index(label_for_address[address]);
        end
        f.print "#{value}, "
        if (address + 1) % 16 == 0
            f.puts
            f.print '    '
        end
    end
    f.puts "};";
end

File::open('watches.h', 'w') do |f|
    f.puts "const uint16_t WATCH_COUNT = #{watches.size};"
    types = ['u8', 'u16', 's8', 's16'];
    types.each.with_index do |x, _|
        f.puts "#define WATCH_#{x.upcase} #{_ + 1}"
    end
    f.puts "const char* WATCH_LABELS[] = {";
    f.puts watches.map { |x| "    \"#{x[:label]}\"" }.join(",\n");
    f.puts "};"
    f.puts "const uint16_t WATCH_ADDRESSES[] = {";
    f.puts watches.map { |x| "    #{sprintf('0x%04x', x[:address])}" }.join(",\n");
    f.puts "};"
    f.puts "const uint8_t WATCH_TYPES[] = {";
    f.puts watches.map { |x| "    #{types.index(x[:type]) + 1}" }.join(",\n");
    f.puts "};"

    f.puts "const uint16_t ARG_WATCH_COUNT = #{arg_watches.size};"
    types = ['u8', 'u16', 's8', 's16'];
    f.puts "const uint16_t ARG_WATCH_ADDRESSES[] = {";
    f.puts arg_watches.map { |x| "    #{sprintf('0x%04x', x[:address])}" }.join(",\n");
    f.puts "};"
    f.puts "const uint8_t ARG_WATCH_TYPES[] = {";
    f.puts arg_watches.map { |x| "    #{types.index(x[:type]) + 1}" }.join(",\n");
    f.puts "};"
    registers = ['A', 'X', 'Y']
    registers.each.with_index do |x, _|
        f.puts "#define WATCH_#{x} #{_ + 1}"
    end
    f.puts "const uint8_t ARG_WATCH_REGISTERS[] = {";
    f.puts arg_watches.map { |x| "    #{registers.index(x[:register]) + 1}" }.join(",\n");
    f.puts "};"
    f.puts "const char* WATCH_REGISTER_LABELS[] = {";
    f.puts "    \"\","
    f.puts registers.map { |x| "    \"#{x}\"" }.join(",\n");
    f.puts "};"
end

puts "Found #{symbols.size} labels and #{watches.size} watches."

# label_for_address.keys.sort.each do |address|
#     puts sprintf("%04x %s", address, label_for_address[address])
# end
