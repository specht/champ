#!/usr/bin/env ruby

vars = {}

File::open(ARGV.first) do |f|
    f.each_line do |line|
        parts = line.split(' ')
        name = parts[1]
        value = parts[2].to_i
        vars[name] ||= []
        vars[name] << value
    end
end

vars.keys.sort.each do |name|
    vars[name].sort!
    format_str = "%-#{vars.keys.map { |x| x.size }.max}s assigned %8d times, min. %6d, max. %6d, median %6d"
    puts sprintf(format_str,
                 name, vars[name].size, vars[name].first, vars[name].last,
                 vars[name][vars[name].size / 2])
end
