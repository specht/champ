#!/usr/bin/env ruby

label_for_address = {}
symbols = []

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
        if symbol =~ /^\w+\s+EQU\s+\$([0-9A-F]{4})/
            address = $1
        end
        symbol = symbol.strip.split(' ').first
        next if address.nil?
        label_for_address[address.to_i(16)] = symbol
        symbols << symbol
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

puts "Found #{symbols.size} labels."

# label_for_address.keys.sort.each do |address|
#     puts sprintf("%04x %s", address, label_for_address[address])
# end
