#usr/bin/python2

import urllib2  
  
try:  
    f = urllib2.urlopen("http://www.python.org")  
    print f.read()  
    f.close()  
except HTTPError, e:  
    print "Ocurrió un error"  
    print e.code  
except URLError, e:  
    print "Ocurrió un error"  
    print e.reason  
