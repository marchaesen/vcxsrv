#!/usr/bin/ruby
#
# $Id$
# The script finds the fragments
#

require "xkbparser.rb"

baseDir = "../.."

symbolsDir = "#{baseDir}/symbols"
#symbolsDir = "."

parser = Parser.new

allSyms = parser.parse("#{symbolsDir}/inet")

everything = allSyms.merge

everything.filter(1)

#numCombinations = 1

#puts "everything:"

#everything.find_all do | symName, keycodes |
#puts "#{symName}, #{keycodes.length} mappings -> "
#  keycodes.find_all do | keycode, counter |
#    puts "  #{keycode} -> #{counter} occurences"
#  end
#  numCombinations *= (keycodes.length + 1)
#end

#puts "Total mappings: #{everything.length}/#{everything.full_length()}, #{numCombinations} combinations"
#

numCombinations = 0
allSyms.find_all do | symsName, symbols |
 puts "n: #{symsName}"

 # Counting only symbols which used more than once
 numDupSymbols = symbols.keys.inject(0) do | rv, keycode |
   c = everything.cardinality(keycode, symbols[keycode])
   puts "#{keycode} -> #{symbols[keycode]}, #{c}"
   (c > 0) ? rv : rv + 1
 end

 numCombinations += (1 << numDupSymbols)
 puts "l: #{symbols.length} d: #{numDupSymbols} c: #{numCombinations}"
end

puts "numCombinations: #{numCombinations}"
