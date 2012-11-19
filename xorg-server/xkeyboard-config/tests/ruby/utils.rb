#
# $Id$
#
# Commont classes
#

#
# The hash containing non-unique mappings
# It can have a->b and a->c together
# Also, for every mapping it counts the number of times this mapping was set
#
class NonuniqueCountingHash < Hash

  alias get_original []
  alias put_original []=

  def []=(key, value)
    own = self.get_original(key)
    hash = get_original(key)
    if hash.nil?
      put_original(key, hash = Hash.new)
    end
    if hash.has_key?(value)
      hash[value] += 1
    else
      hash[value] = 1
    end
  end
  
  #
  # Number of all mappings (a->b and a->c counted as 2 mappings)
  #
  def full_length()
    values.inject(0) do | rv, hash |
      rv + hash.length
    end
  end

  def cardinality(key1, key2)
    if has_key?(key1) 
      hash = get_original(key1)
      if hash.has_key?(key2)
        hash[key2]
      else
        0
      end
    else
      0
    end
  end

  def filter(limit)
    find_all do | key, hash |
      hash.find_all do | key1, counter |
        if (counter <= limit)
          hash.delete(key1)
        end
      end
      if hash.empty? 
        delete(key)
      end
    end
  end
end
