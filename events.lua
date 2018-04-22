
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
        local event_mod = math.random(5) / 100 * food;
        food = food - event_mod;
        gd_set_food(food);
        gd_print_yellow("Galactic weevils infest crops, " .. math.floor(event_mod) .. " food lost.");                        
    end
end

if (gd_in_protection() == 0) then

    local pirate_rand = math.random(100);

    if pirate_rand < 15 then
        local troops = gd_get_troops();
        local fighters = gd_get_defence_stations();
        local generals = gd_get_generals();

        local pirate_troops;
        if troops / 100 >= 1 then
            pirate_troops = math.floor(troops + (math.random(math.floor(troops / 100)) - (troops / 100 / 2)));
        else 
            pirate_troops = 0;
        end
        local pirate_fighters;
        if fighters / 100 >= 1 then 
            piirate_fighters = math.floor(fighters + (math.random(math.floor(fighters / 100)) - (fighters / 100 / 2)));
        else
            pirate_fighters = 0;
        end

        local pirate_generals;
        if generals / 100 >= 1 then
            pirate_generals = math.floor(generals + (math.random(math.floor(generals / 100)) - (generals / 100 / 2)));
        else
            pirate_generals = 0;
        end

        local battle = math.random(100);

        if battle < 50 then
            -- pirate victory
            local food = gd_get_food()
            local population = gd_get_population()
            local credits = gd_get_credits()
            local planets = gd_get_planets();

            local difference = math.random(50);
            local dtroops = math.floor(pirate_troops - (pirate_troops * (difference / 100)));
            local dgenerals = math.floor(pirate_generals - (pirate_generals * (difference / 100)));
            local dfighters = math.floor(pirate_fighters - (pirate_fighters * (difference / 100)));
            
            if dtroops > troops then
                dtroops = troops
            end
            if dgenerals > generals then
                dgenerals = generals;
            end
            if dfighters > fighters then 
                dfighters = fighters;			
            end
            gd_set_troops(troops - dtroops);
            gd_set_generals(generals - dgenerals);
            gd_set_defence_stations(fighters - dfighters);

            local plunder_food = math.floor(food / 10);
            local plunder_population = math.floor(population / 10);
            local plunder_credits = math.floor(credits / 10);
            local plunder_planets = math.floor(planets /  10);

            gd_set_food(food - plunder_food);
            gd_set_population(population - plunder_population);
            gd_set_credits(credits - plunder_credits);
            plunder_planets = gd_destroy_planets(plunder_planets);

            gd_print_red("PIRATE ATTACK!!")
            gd_print_red(dtroops .. " troops, " .. dgenerals .. " generals, " .. dfighters .. " defence stations destroyed.");
            gd_print_red(plunder_food .. " food, " .. plunder_population .. " citizens, " .. plunder_credits .. " credits stolen.");
            gd_print_red(math.floor(plunder_planets) .. " planets destroyed.");

        else
            local difference = math.random(50);
            local dtroops = math.floor(pirate_troops - (pirate_troops * (difference / 100)));
            local dgenerals = math.floor(pirate_generals - (pirate_generals * (difference / 100)));
            local dfighters = math.floor(pirate_fighters - (pirate_fighters * (difference / 100)));
            
            if dtroops > troops then
                dtroops = troops
            end
            if dgenerals > generals then
                dgenerals = generals;
            end
            if dfighters > fighters then 
                dfighters = fighters;			
            end
            gd_set_troops(troops - dtroops);
            gd_set_generals(generals - dgenerals);
            gd_set_defence_stations(fighters - dfighters);
            if dtroops > 0 or dgenerals > 0 or dfighters > 0 then
                gd_print_red("PIRATE ATTACK!!")
                gd_print_red(dtroops .. " troops, " .. dgenerals .. " generals, " .. dfighters .. " defence stations destroyed.");
            end
        end
    end
end
