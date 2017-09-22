
-- seed random number generator
math.randomseed(os.time());

-- Chance of an event occuring, 25%
local event_rand = math.random(100);

-- Which event should occur
local total_events = 8;
local event = math.random(total_events)

-- If chance is 25% or less
if event_rand < 25 then
    -- do the event selected
    if event == 1 then
        local population = gd_get_population();
        local event_mod = math.random(5) / 100 * population;
        population = population - event_mod;
        gd_set_population(population);
        gd_print_yellow("A new plague sweeps through your empire killing " .. math.floor(event_mod) .. " citizens.");
    elseif event == 2 then
        local credits = gd_get_credits();
        local event_mod = math.random(5) / 100 * credits;
        credits = credits - event_mod;
        gd_set_credits(credits);
        gd_print_yellow("Rogue hackers attack empire banks! You lost " .. math.floor(event_mod) .. " credits.");    
    elseif event == 3 then
        local troops = gd_get_troops();
        local event_mod = math.random(5) / 100 * troops;
        troops = troops - event_mod;
        gd_set_troops(troops);
        gd_print_yellow("Civil war breaks out! You lost " .. math.floor(event_mod) .. " troops.");    
    elseif event == 4 then
        local population = gd_get_population();
        local event_mod = math.random(5) / 100 * population;
        population = population + event_mod;
        gd_set_population(population);
        gd_print_green("Citizen confidence at an all time high, population increased by " .. math.floor(event_mod) .. " citizens.");    
    elseif event == 5 then
        local credits = gd_get_credits();
        local event_mod = math.random(5) / 100 * credits;
        credits = credits + event_mod;
        gd_set_credits(credits);
        gd_print_green("Markets booming! Stocks return an extra " .. math.floor(event_mod) .. " credits.");    
    elseif event == 6 then
        local troops = gd_get_troops();
        local event_mod = math.random(5) / 100 * troops;
        troops = troops + event_mod;
        gd_set_troops(troops);
        gd_print_green("Recruitment propaganda pays off, " .. math.floor(event_mod) .. " troops enlist.");        
    elseif event == 7 then
        local food = gd_get_food();
        local event_mod = math.random(5) / 100 * food;
        food = food + event_mod;
        gd_set_food(food);
        gd_print_green("Bumper harvests! Farmers produce an extra, " .. math.floor(event_mod) .. " food.");        
    elseif event == 8 then
        local food = gd_get_food();
        local event_mod = math.random(5) / 100 * troops;
        food = food - event_mod;
        gd_set_food(food);
        gd_print_yellow("Galactic weevils infest crops, " .. math.floor(event_mod) .. " food lost.");                        
    end
end
