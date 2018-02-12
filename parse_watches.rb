#!/usr/bin/env ruby

vars = {}

File::open(ARGV.first) do |f|
    f.each_line do |line|
        if line[0] == '~'
            parts = line.split(' ')
            name = parts[2]
            value = parts[3].to_i
            vars[name] ||= []
            vars[name] << value
        elsif line[0] == '@'
            address = line.split(' ')[1].sub('0x', '').to_i(16)
            rest = line[line.index(' ', 2), line.size].strip
            rest.split(',').each do |part|
                part.strip!
#                 puts part
            end
        end
    end
end

vars.keys.sort.each do |name|
    vars[name].sort!
    format_str = "%-#{vars.keys.map { |x| x.size }.max}s assigned %8d times, min. %6d, max. %6d, median %6d"
    puts sprintf(format_str,
                 name, vars[name].size, vars[name].first, vars[name].last,
                 vars[name][vars[name].size / 2])
end
