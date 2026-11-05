#include "../r67_pr50_queue_policy.hpp"
#include <assert.h>
#include <stdio.h>
int main() {
    assert(cores3_pr50_c140_budget_target(14)==24);
    assert(cores3_pr50_c140_budget_target(13)==24);
    assert(cores3_pr50_c140_budget_target(12)==24);
    assert(cores3_pr50_c140_budget_target(11)==20);
    assert(cores3_pr50_c140_budget_target(9)==16);
    assert(cores3_pr50_c140_budget_target(7)==12);
    assert(cores3_pr50_c140_budget_target(5)==8);
    assert(cores3_pr50_c140_budget_target(3)==4);
    assert(cores3_pr50_c140_budget_target(1)==0);
    assert(cores3_pr50_c140_budget_target(0)==0);
    assert(cores3_pr50_should_generate(0));
    assert(cores3_pr50_should_generate(13));
    assert(!cores3_pr50_should_generate(14));
    assert(cores3_pr50_wake_producer(12));
    assert(cores3_pr50_wake_producer(0));
    assert(!cores3_pr50_wake_producer(13));

    unsigned budget=24,recovery=0;
    const int falling[]={11,9,7,5,3,1};
    const unsigned fallingBudgets[]={20,16,12,8,4,0};
    for (unsigned i=0;i<6;i++) {
        budget=cores3_pr50_update_c140_budget(falling[i],budget,&recovery);
        assert(budget==fallingBudgets[i] && recovery==0);
    }
    // Avoid restoring voices during marginal queue oscillations.
    for (unsigned i=0;i<100;i++) {
        budget=cores3_pr50_update_c140_budget(11,budget,&recovery);
        assert(budget==0 && recovery==0);
    }
    // Recovery is deliberately gradual at sustained >=12 queue.
    for (unsigned target=4;target<=24;target+=4) {
        for (unsigned j=0;j<31;j++) {
            budget=cores3_pr50_update_c140_budget(12,budget,&recovery);
            assert(budget==target-4);
        }
        budget=cores3_pr50_update_c140_budget(12,budget,&recovery);
        assert(budget==target && recovery==0);
    }
    // A pressure dip resets recovery rather than immediately
    // restoring all voices (causing load oscillation).
    budget=cores3_pr50_update_c140_budget(10,budget,&recovery);
    assert(budget==20);
    for (unsigned i=0;i<10;i++)
        budget=cores3_pr50_update_c140_budget(12,budget,&recovery);
    assert(budget==20 && recovery==10);
    budget=cores3_pr50_update_c140_budget(11,budget,&recovery);
    assert(budget==20 && recovery==0);

    puts("PASS PR50 14/12 PCM queue, early C140 24/20/16/12/8/4/0 adaptation, hysteretic recovery");
    return 0;
}
