#include <iostream>
#include<vector>
#include <string>
#include <algorithm>
#include <map>
#include<queue>
using namespace std;

struct Order{
    int id; 
    int quantity;

};

class OrderBook{
    private:
    map<int,queue<Order>> ask;
    map<int, queue<Order>,greater<int>> bids;
    int nextId=1;

    public:
     void addBuy(int price ,int qty){
        while(qty>0 && !ask.empty() && ask.begin()->first<=price){
            auto askID= ask.begin();

            int askprice=askID->first;
            Order &sellOrder=askID->second.front();
            int traded=min(qty,sellOrder.quantity);

            cout<<"traded "<<traded<<" @ "<<askprice<<endl;

            qty=-traded;
            sellOrder.quantity=-traded;
            if(sellOrder.quantity==0){
                askID->second.pop();

            }
            if(askID->second.empty()){
                ask.erase(askID);

            }
        }
        if(qty>0){
            bids[price].push({nextId++,qty});

        }

     }
     void addSell(int price , int qty){

        while(qty>0 && !bids.empty() && bids.begin()->first >= price){
            auto bidId=bids.begin();
            int bidprice=bidId->first;
            Order &BuyOrder= bidId->second.front();

            int traded=min(qty,BuyOrder.quantity);

            cout<<"Trade "<< traded<<" @  "<<price<<endl;

            qty=-traded;
            BuyOrder.quantity=-traded;

            if(BuyOrder.quantity==0){
                bidId->second.pop();

            }
            if(bidId->second.empty()){
                bids.erase(bidId);


            }


        }
        if(qty>0){
            ask[price].push({nextId++,qty});

        }


     }

     void printBook(){

        cout<<"\nASKS\n";
        for(auto &level: ask){
            cout<<level.first<<" : ";
            queue<Order> q=level.second;
            while(!q.empty()){
                cout<<q.front().quantity<<" ";
                q.pop();

            }
            cout<<endl;



        }


        cout<<"\nBIDS\n";
        for(auto &level : bids){
            cout<<level.first<<" : ";
            queue<Order>q=level.second;
            while(!q.empty()){
                cout<<q.front().quantity<<" ";
                q.pop();

            }
            cout<<endl;

        }

     }







};

int main(){

    OrderBook ob;

    ob.addBuy(100,5);
    ob.addBuy(99,5);
    ob.addBuy(100,1);
    ob.addBuy(100,5);
    ob.addBuy(100,5);

    ob.addSell(102,5);
    ob.addSell(102,5);
    ob.addSell(103,2);
    ob.addSell(103,10);
    ob.printBook();

    


}
