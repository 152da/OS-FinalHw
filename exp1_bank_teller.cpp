#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using namespace std;

class Semaphore {
private:
    mutex mtx;
    condition_variable cv;
    int count;

public:
    explicit Semaphore(int init_count = 0) : count(init_count) {}

    void P() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [&]() {
            return count > 0;
        });
        count--;
    }

    void V() {
        unique_lock<mutex> lock(mtx);
        count++;
        cv.notify_one();
    }
};

struct Customer {
    int id;
    double arrive_time;
    double service_time;

    int number = -1;
    int teller_id = -1;

    double enter_time = 0;
    double start_time = 0;
    double leave_time = 0;

    Semaphore finished;

    Customer(int id_, double arrive_time_, double service_time_)
        : id(id_),
          arrive_time(arrive_time_),
          service_time(service_time_),
          finished(0) {}
};

queue<Customer*> waiting_queue;

Semaphore queue_mutex(1);
Semaphore customer_count(0);

mutex cout_mutex;

int next_number = 1;

chrono::steady_clock::time_point start_time;

double now_seconds() {
    auto now = chrono::steady_clock::now();
    chrono::duration<double> diff = now - start_time;
    return diff.count();
}

void sleep_until_relative(double target_time) {
    auto target = start_time + chrono::duration_cast<chrono::steady_clock::duration>(
                                 chrono::duration<double>(target_time));
    this_thread::sleep_until(target);
}

void sleep_for_seconds(double seconds) {
    this_thread::sleep_for(chrono::duration<double>(seconds));
}

void print_event(const string& msg) {
    lock_guard<mutex> lock(cout_mutex);
    cout << fixed << setprecision(3)
         << "[time " << setw(7) << right << now_seconds() << "] "
         << msg << endl;
}

void customer_thread(Customer* customer) {
    sleep_until_relative(customer->arrive_time);

    customer->enter_time = now_seconds();

    queue_mutex.P();//取号
    customer->number=next_number++;
    waiting_queue.push(customer);

    queue_mutex.V();
    print_event("顾客"+to_string(customer->id)+"到达银行，取得号码"+to_string(customer->number));

    customer_count.V();//加入等待队列
    customer->finished.P();//完成办理
    print_event("顾客 " + to_string(customer->id) +
                " 办理完成，离开银行");
}

void teller_thread(int teller_id)
{
    while(true)
    {
        customer_count.P();//请求等待队列

        queue_mutex.P();//从等待队列中取出顾客

        Customer*customer = waiting_queue.front();
        waiting_queue.pop();

        queue_mutex.V();

        if(customer == nullptr)
        {
            print_event("柜员 " + to_string(teller_id) + " 下班");
            break;
        }

        customer->teller_id = teller_id;
        customer->start_time=now_seconds();

        print_event("柜员 " + to_string(teller_id) +
                    " 开始服务顾客 " + to_string(customer->id) +
                    "，号码 " + to_string(customer->number));

        sleep_for_seconds(customer->service_time);

        customer->leave_time = now_seconds();
        print_event("柜员 " + to_string(teller_id) +
                    " 完成顾客 " + to_string(customer->id) +
                    " 的服务");

        customer->finished.V();//完成服务
    }
}

vector<shared_ptr<Customer>> read_customer(const string&filename)
{
    ifstream fin(filename);
    
    if(!fin.is_open())
    {
        cerr<<"无法打开测试文件："<<filename<<endl;
        exit(1);
    }

    vector<shared_ptr<Customer>> customers;

    int id;
    double arrive_time;
    double service_time;

    while(fin>>id>>arrive_time>>service_time)
    {
        customers.push_back(make_shared<Customer>(id,arrive_time,service_time));
    }
    
     sort(customers.begin(), customers.end(),
         [](const shared_ptr<Customer>& a, const shared_ptr<Customer>& b) {
             if (a->arrive_time != b->arrive_time) {
                 return a->arrive_time < b->arrive_time;
             }
             return a->id < b->id;
         });
         return customers;

}
void print_result(const vector<shared_ptr<Customer>>& customers) {
    cout << endl;
    cout << "========================= Final Result =========================" << endl;
    cout << "Time unit: second" << endl;
    cout << left
         << setw(12) << "Customer"
         << setw(10) << "Number"
         << setw(14) << "Enter"
         << setw(14) << "Start"
         << setw(14) << "Leave"
         << setw(10) << "Teller"
         << endl;

    cout << string(74, '-') << endl;

    cout << fixed << setprecision(3);

    for (const auto& customer : customers) {
        cout << left
             << setw(12) << customer->id
             << setw(10) << customer->number
             << setw(14) << customer->enter_time
             << setw(14) << customer->start_time
             << setw(14) << customer->leave_time
             << setw(10) << customer->teller_id
             << endl;
    }
}

int main(int argc,char*argv[])
{
    int teller_num;
    string filename;

    if(argc>=3)
    {
        teller_num=stoi(argv[1]);
        filename =argv[2];
    }else 
    {
         cout << "请输入柜员数量: ";
        cin >> teller_num;

        cout << "请输入测试文件名: ";
        cin >> filename;
    }

    if(teller_num<=0)
    {
        cerr<<"柜员数量必须大于0"<<endl;
        return 1;
    }
      vector<shared_ptr<Customer>> customers = read_customer(filename);

    if (customers.empty()) {
        cerr << "测试文件中没有顾客数据" << endl;
        return 1;
    }
    start_time = chrono::steady_clock::now();

    vector<thread> tellers;
    vector<thread> customer_threads;

    for(int i=1;i<=teller_num;i++)
    {
        tellers.emplace_back(teller_thread,i);
    }
    for(auto&customer:customers)
    {
        customer_threads.emplace_back(customer_thread,customer.get());
    }
    for(auto&th:customer_threads)
    {
        th.join();
    }
    for (int i = 0; i < teller_num; i++) {
        queue_mutex.P();
        waiting_queue.push(nullptr);
        queue_mutex.V();

        customer_count.V();
    }
    for (auto& th : tellers) {
        th.join();
    }

    print_result(customers);
    return 0;
}
