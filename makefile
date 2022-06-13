webserver: main.cpp ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp ./time/m_time.h
	g++ -o webserver ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp ./time/m_time.h main.cpp -lpthread -lhiredis -std=c++11

temp : main.cpp ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp
	g++ -E main.cpp -o server.i  -lpthread -lhiredis -std=c++11
clean:
	rm  -r server
	
websever: main.cpp ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp ./time/m_time.h
	g++ -o websever ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp ./time/m_time.h main.cpp -lpthread -lhiredis -std=c++11
